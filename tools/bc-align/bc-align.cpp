#include <string>
#include <utility>

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/LLVMBitCodes.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include "BitstreamReader.h"
#include "BitstreamWriter.h"

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

static unsigned AlignSize(unsigned size) {
  while (size % 8)
    size++;
  return size;
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
                             AlignSize(Op.getEncodingData()));
        break;
      case BitCodeAbbrevOp::VBR:
        Op = BitCodeAbbrevOp(BitCodeAbbrevOp::VBR,
                             AlignSize(Op.getEncodingData()));
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

namespace {
class AlignedBitstreamWriter : public BitstreamWriter {
protected:
  void EmitArrayLen(uint32_t Val) override { EmitVBRAligned(Val, 6, 8); }

public:
  AlignedBitstreamWriter(SmallVectorImpl<char> &O) : BitstreamWriter(O) {}

  unsigned CalculateVBRSize(uint64_t Val, unsigned NumBits) {
    // Calculate how many bits will be used to emit a VBR value.
    unsigned Result = NumBits;
    uint32_t Threshold = 1U << (NumBits - 1);
    while (Val >= Threshold) {
      Result += NumBits;
      Val >>= NumBits - 1;
    }
    return Result;
  }

  void EmitVBRAligned(uint64_t Val, unsigned NumBits, unsigned ExtraBits = 0) {
    // Emit a VBR value in such a way that after the value is emitted, and
    // ExtraBits additional bits have been emitted, the stream is byte-aligned.
    assert(NumBits <= 32 && "Too many bits to emit!");
    uint32_t Threshold = 1U << (NumBits - 1);
    uint32_t Mask = Threshold - 1;
    while (Val >= Threshold) {
      Emit(((uint32_t)Val & Mask) | Threshold, NumBits);
      Val >>= NumBits - 1;
    }
    int Attempts = 0;
    while ((GetCurrentBitNo() + NumBits + ExtraBits) % 8) {
      Emit(((uint32_t)Val & Mask) | Threshold, NumBits);
      Val >>= NumBits - 1;
      if (++Attempts >= 7) {
        // Failed to align the value. This can happen e.g. if NumBits is even
        // but we started at an odd bit position.
        break;
      }
    }
    Emit((uint32_t)Val, NumBits);
  }

  void EncodeAbbrev(const BitCodeAbbrev &Abbv) override {
    // Emit a DEFINE_ABBREV so that the stream is byte-aligned afterwards. The
    // only fully general way to do this is to emit numabbrevops using
    // EmitVBRAligned with an ExtraBits value that accounts for the remainder
    // of the DEFINE_ABBREV.

    // Calculate the ExtraBits (the number of bits that will be emitted after
    // numabbrevops).
    unsigned e = static_cast<unsigned>(Abbv.getNumOperandInfos());
    unsigned ExtraBits = 0;
    for (unsigned i = 0; i != e; ++i) {
      const BitCodeAbbrevOp &Op = Abbv.getOperandInfo(i);
      ExtraBits += 1;
      if (Op.isEncoding()) {
        ExtraBits += 3;
        if (Op.hasEncodingData())
          ExtraBits += CalculateVBRSize(Op.getEncodingData(), 5);
      }
      // Literals add a multiple of 8 bits, so they don't matter.
    }

    EmitCode(bitc::DEFINE_ABBREV);
    EmitVBRAligned(Abbv.getNumOperandInfos(), 5, ExtraBits);
    for (unsigned i = 0; i != e; ++i) {
      const BitCodeAbbrevOp &Op = Abbv.getOperandInfo(i);
      Emit(Op.isLiteral(), 1);
      if (Op.isLiteral()) {
        EmitVBR64(Op.getLiteralValue(), 8);
      } else {
        Emit(Op.getEncoding(), 3);
        if (Op.hasEncodingData())
          EmitVBR64(Op.getEncodingData(), 5);
      }
    }
  }

  void EmitRecordAligned(unsigned Abbrev, unsigned Code,
                         SmallVectorImpl<uint64_t> &Vals, StringRef Blob) {
    if (!Blob.empty()) {
      BitstreamWriter::EmitRecordWithAbbrevImpl(Abbrev, makeArrayRef(Vals),
                                                Blob, Code);
      return;
    }

    if (!Abbrev) {
      auto Count = static_cast<uint32_t>(Vals.size());
      EmitCode(bitc::UNABBREV_RECORD);
      EmitVBR(Code, 6);
      EmitVBRAligned(Count, 6);
      for (unsigned i = 0, e = Count; i != e; ++i)
        EmitVBRAligned(Vals[i], 6);
      return;
    }

    BitstreamWriter::EmitRecord(Code, Vals, Abbrev);
  }
};
} // end anonymous namespace

namespace {
class BitcodeAligner {
public:
  BitcodeAligner(MemoryBufferRef InBuffer, SmallVectorImpl<char> &OutBuffer);
  void AlignBitcode();

private:
  BitstreamCursor Reader;
  AlignedBitstreamWriter Writer;
  BitstreamBlockInfo BlockInfo;
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
  Writer.EnterSubblock(ID, AlignSize(Reader.getAbbrevIDWidth()));
}

void BitcodeAligner::HandleEndBlock() { Writer.ExitBlock(); }

void BitcodeAligner::HandleBlockinfoBlock() {
  Optional<BitstreamBlockInfo> NewBlockInfo = Reader.ReadBlockInfoBlock();
  if (!NewBlockInfo)
    report_fatal_error("Malformed BlockInfoBlock");
  BlockInfo = std::move(*NewBlockInfo);

  Writer.EnterBlockInfoBlock();
  for (size_t i = 0, e = BlockInfo.getNumBlockInfos(); i != e; i++) {
    const BitstreamBlockInfo::BlockInfo *BI = BlockInfo.getBlockInfoByIndex(i);
    for (const auto &Abbrev : BI->Abbrevs)
      Writer.EmitBlockInfoAbbrev(BI->BlockID, AlignAbbrev(Abbrev.get()));
  }
  Writer.ExitBlock();
}

void BitcodeAligner::HandleDefineAbbrev() {
  const BitCodeAbbrev *Abbrev = Reader.ReadAbbrevRecord();
  Writer.EmitAbbrev(AlignAbbrev(Abbrev));
}

void BitcodeAligner::HandleRecord(unsigned ID) {
  SmallVector<uint64_t, 64> Record;
  StringRef Blob;
  unsigned Code = Reader.readRecord(ID, Record, &Blob);
  unsigned Abbrev = ID != bitc::UNABBREV_RECORD ? ID : 0;

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
