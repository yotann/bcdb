#include <string>
#include <utility>

#include <llvm/ADT/IndexedMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/LLVMBitCodes.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MathExtras.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include "AlignedBitstreamWriter.h"
#include "BitstreamReader.h"

// WARNING: this code could break (generate invalid modules) if LLVM ever adds
// more file offsets!

using namespace bcdb;
using namespace llvm;

static cl::opt<std::string>
    InputFilename(cl::Positional, cl::desc("<input bitcode>"), cl::init("-"));

static cl::opt<std::string> OutputFilename("o",
                                           cl::desc("Override output filename"),
                                           cl::value_desc("filename"));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"));

static void WriteOutputFile(const SmallVectorImpl<char> &Buffer) {
  // Infer the output filename if needed.
  if (OutputFilename.empty()) {
    if (InputFilename == "-") {
      OutputFilename = "-";
    } else {
      StringRef IFN = InputFilename;
      OutputFilename = (IFN.endswith(".bc") ? IFN.drop_back(3) : IFN).str();
      OutputFilename += ".aligned.bc";
    }
  }

  std::error_code EC;
  std::unique_ptr<tool_output_file> Out(
      new tool_output_file(OutputFilename, EC, sys::fs::F_None));
  if (EC) {
    errs() << EC.message() << '\n';
    exit(1);
  }

  if (Force || !CheckBitcodeOutputToConsole(Out->os(), true))
    Out->os().write(Buffer.data(), Buffer.size());

  // Declare success.
  Out->keep();
}

static bool AbbrevWorthKeeping(const BitCodeAbbrev *Abbrev) {
  for (unsigned i = 0; i < Abbrev->getNumOperandInfos(); i++) {
    BitCodeAbbrevOp Op = Abbrev->getOperandInfo(i);
    if (Op.isEncoding()) {
      switch (Op.getEncoding()) {
      case BitCodeAbbrevOp::Fixed:
        // also used for backpatching
        if (Op.getEncodingData() >= 8) return true;
        break;
      case BitCodeAbbrevOp::Blob:
        // required
        return true;
      case BitCodeAbbrevOp::Array:
        return false;
      default:
        break;
      }
    }
  }
  return false;
}

static std::shared_ptr<BitCodeAbbrev> AlignAbbrev(const BitCodeAbbrev *Abbrev) {
  auto Result = std::make_shared<BitCodeAbbrev>();
  for (unsigned i = 0; i < Abbrev->getNumOperandInfos(); i++) {
    BitCodeAbbrevOp Op = Abbrev->getOperandInfo(i);
    if (Op.isEncoding()) {
      switch (Op.getEncoding()) {
      case BitCodeAbbrevOp::Fixed:
        // We could try to keep track of multiple fixed fields, and only
        // adjust one of them for alignment, but that wouldn't work well for
        // LLVM's abbrevs in practice.
        Op = BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                             alignTo<8>(Op.getEncodingData()));
        break;
      case BitCodeAbbrevOp::VBR:
        Op = BitCodeAbbrevOp(BitCodeAbbrevOp::VBR,
                             alignTo<8>(Op.getEncodingData()));
        break;
      case BitCodeAbbrevOp::Array:
        // Arrays have a vbr6 length field, which is forced to be aligned by
        // AlignedBitstreamWriter.
        break;
      case BitCodeAbbrevOp::Char6:
        Op = BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8);
        break;
      case BitCodeAbbrevOp::Blob:
        // Blobs already have alignment by default!
        break;
      }
    }
    Result->Add(Op);
  }
  return std::move(Result);
}

static std::shared_ptr<BitCodeAbbrev> MakeGeneralAbbrev() {
  auto Result = std::make_shared<BitCodeAbbrev>();
  Result->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  Result->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Result->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  return std::move(Result);
}

namespace {
class BitcodeAligner {
public:
  BitcodeAligner(MemoryBufferRef InBuffer, SmallVectorImpl<char> &OutBuffer);
  void AlignBitcode();

private:
  BitstreamCursor Reader;
  AlignedBitstreamWriter Writer;
  BitstreamBlockInfo BlockInfo;

  struct Block {
    IndexedMap<unsigned> AbbrevIDMap;
    unsigned GeneralAbbrevID = 0;
  };
  SmallVector<Block, 0> Blocks;

  uint64_t VSTOffsetPlaceholder = 0;
  uint32_t VSTOffsetOldValue = 0;
  DenseMap<uint64_t, uint64_t> OffsetMap;
  uint64_t CurEntryInOffset, CurEntryOutOffset;

  void HandleStartBlock(unsigned ID);
  void HandleEndBlock();
  void HandleBlockinfoBlock();
  void HandleDefineAbbrev();
  void HandleRecord(unsigned ID);
};
} // end anonymous namespace

BitcodeAligner::BitcodeAligner(MemoryBufferRef InBuffer,
                               SmallVectorImpl<char> &OutBuffer)
    : Reader(InBuffer), Writer(OutBuffer) {
  // Skip the wrapper header, if any.
  const unsigned char *BufPtr =
      reinterpret_cast<const unsigned char *>(InBuffer.getBufferStart());
  const unsigned char *EndBufPtr =
      reinterpret_cast<const unsigned char *>(InBuffer.getBufferEnd());
  if (isBitcodeWrapper(BufPtr, EndBufPtr)) {
    if (SkipBitcodeWrapperHeader(BufPtr, EndBufPtr, true))
      report_fatal_error("Invalid bitcode wrapper");
    Reader = BitstreamCursor(ArrayRef<uint8_t>(BufPtr, EndBufPtr));
  }

  Reader.setBlockInfo(&BlockInfo);
}

void BitcodeAligner::HandleStartBlock(unsigned ID) {
  if (ID == bitc::FUNCTION_BLOCK_ID) {
    OffsetMap[CurEntryInOffset] = Writer.GetCurrentBitNo();
  } else if (ID == bitc::VALUE_SYMTAB_BLOCK_ID && VSTOffsetPlaceholder) {
    if (VSTOffsetOldValue == CurEntryInOffset / 32) {
      Writer.BackpatchWord(VSTOffsetPlaceholder, CurEntryOutOffset / 32);
    }
  }
  if (Reader.EnterSubBlock(ID, nullptr))
    report_fatal_error("Malformed block record");
  Writer.EnterSubblock(ID, alignTo<8>(Reader.getAbbrevIDWidth() + 1)); // +1 to accomodate the general abbrev if we add it

  Blocks.emplace_back();
  const auto *BI = BlockInfo.getBlockInfo(ID);
  Blocks.back().AbbrevIDMap.grow(bitc::FIRST_APPLICATION_ABBREV - 1);
  if (BI) {
    unsigned i = bitc::FIRST_APPLICATION_ABBREV;
    unsigned e = bitc::FIRST_APPLICATION_ABBREV + BI->Abbrevs.size();
    Blocks.back().AbbrevIDMap.grow(e - 1);
    unsigned j = i;
    for(; i != e; i++)
      if (AbbrevWorthKeeping(BI->Abbrevs[i - bitc::FIRST_APPLICATION_ABBREV].get()))
        Blocks.back().AbbrevIDMap[i] = j++;
  }

  Blocks.back().GeneralAbbrevID = Writer.EmitAbbrev(MakeGeneralAbbrev());
}

void BitcodeAligner::HandleEndBlock() {
  Writer.ExitBlock();
  Blocks.pop_back();
}

void BitcodeAligner::HandleBlockinfoBlock() {
  Optional<BitstreamBlockInfo> NewBlockInfo = Reader.ReadBlockInfoBlock();
  if (!NewBlockInfo)
    report_fatal_error("Malformed BlockInfoBlock");
  BlockInfo = std::move(*NewBlockInfo);

  Writer.EnterBlockInfoBlock();
  for (size_t i = 0, e = BlockInfo.getNumBlockInfos(); i != e; i++) {
    const BitstreamBlockInfo::BlockInfo *BI = BlockInfo.getBlockInfoByIndex(i);
    for (const auto &Abbrev : BI->Abbrevs)
      if (AbbrevWorthKeeping(Abbrev.get()))
        Writer.EmitBlockInfoAbbrev(BI->BlockID, AlignAbbrev(Abbrev.get()));
  }
  Writer.ExitBlock();
}

void BitcodeAligner::HandleDefineAbbrev() {
  unsigned InAbbrevID = Reader.ReadAbbrevRecord();
  const BitCodeAbbrev *Abbrev = Reader.getAbbrev(InAbbrevID);
  Blocks.back().AbbrevIDMap.grow(InAbbrevID);
  if (!AbbrevWorthKeeping(Abbrev)) return;
  unsigned OutAbbrevID = Writer.EmitAbbrev(AlignAbbrev(Abbrev));
  Blocks.back().AbbrevIDMap[InAbbrevID] = OutAbbrevID;
}

void BitcodeAligner::HandleRecord(unsigned ID) {
  SmallVector<uint64_t, 64> Record;
  StringRef Blob;
  unsigned Code = Reader.readRecord(ID, Record, &Blob);
  unsigned Abbrev = Blocks.back().AbbrevIDMap[ID];
  if (!Abbrev) {
    if (!Blocks.back().GeneralAbbrevID)
      Blocks.back().GeneralAbbrevID = Writer.EmitAbbrev(MakeGeneralAbbrev());
    Abbrev = Blocks.back().GeneralAbbrevID;
  }

  // Fix FNENTRY offsets to point to the new offset.
  if (Reader.getBlockID() == bitc::VALUE_SYMTAB_BLOCK_ID &&
      Code == bitc::VST_CODE_FNENTRY && Record.size() >= 2) {
    Record[1] = OffsetMap[Record[1] * 32] / 32;
  }

  if (Reader.getBlockID() == bitc::METADATA_BLOCK_ID &&
      (Code == bitc::METADATA_INDEX_OFFSET || Code == bitc::METADATA_INDEX)) {
    // Just omit the metadata index. We don't need it, and
    // METADATA_INDEX_OFFSET is a pain to update.
    return;
  } else if (Reader.getBlockID() == bitc::MODULE_BLOCK_ID &&
             Code == bitc::MODULE_CODE_VSTOFFSET && Record.size() == 1) {
    // Leave a placeholder to be updated later.
    VSTOffsetOldValue = Record[0];
    Record[0] = 0;
    Writer.EmitRecordAligned(Abbrev, Code, Record, Blob);
    VSTOffsetPlaceholder = Writer.GetCurrentBitNo() - 32;
  } else {
    Writer.EmitRecordAligned(Abbrev, Code, Record, Blob);
  }
}

void BitcodeAligner::AlignBitcode() {
  uint32_t Signature = Reader.Read(32);
  Writer.Emit(Signature, 32);

  while (!Reader.AtEndOfStream()) {
    CurEntryInOffset = Reader.GetCurrentBitNo();
    CurEntryOutOffset = Writer.GetCurrentBitNo();
    BitstreamEntry Entry =
        Reader.advance(BitstreamCursor::AF_DontAutoprocessAbbrevs);

    if (Entry.Kind == BitstreamEntry::EndBlock) {
      HandleEndBlock();
    } else if (Entry.Kind == BitstreamEntry::SubBlock &&
               Entry.ID == bitc::BLOCKINFO_BLOCK_ID) {
      HandleBlockinfoBlock();
    } else if (Entry.Kind == BitstreamEntry::SubBlock) {
      HandleStartBlock(Entry.ID);
    } else if (Entry.Kind == BitstreamEntry::Record &&
               Entry.ID == bitc::DEFINE_ABBREV) {
      HandleDefineAbbrev();
    } else if (Entry.Kind == BitstreamEntry::Record) {
      HandleRecord(Entry.ID);
    } else {
      report_fatal_error("Malformed bitstream entry");
    }
  }
}

int main(int argc, const char **argv) {
  cl::ParseCommandLineOptions(argc, argv, "bitcode aligner");

  ErrorOr<std::unique_ptr<MemoryBuffer>> MemBufOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFilename);
  if (std::error_code EC = MemBufOrErr.getError()) {
    errs() << "Error reading '" << InputFilename << "': " << EC.message();
    return 1;
  }
  std::unique_ptr<MemoryBuffer> MemBuf(std::move(MemBufOrErr.get()));

  SmallVector<char, 0> OutBuffer;
  OutBuffer.reserve(256 * 1024);
  BitcodeAligner(*MemBuf, OutBuffer).AlignBitcode();
  WriteOutputFile(OutBuffer);

  return 0;
}
