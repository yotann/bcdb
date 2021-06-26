#include "bcdb/AlignBitcode.h"

#include <llvm/ADT/IndexedMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Bitcode/LLVMBitCodes.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MathExtras.h>
#include <llvm/Support/MemoryBuffer.h>
#include <utility>

#include "AlignedBitstreamWriter.h"
#include "BitstreamReader.h"

// WARNING: this code could break (generate invalid modules) if LLVM ever adds
// more file offsets to the bitstream!

using namespace bcdb;
using namespace llvm;

static Error error(const Twine &Message) {
  return make_error<StringError>(Message, errc::invalid_argument);
}

// jscpd:ignore-start

static bool AbbrevWorthKeeping(const BitCodeAbbrev *Abbrev) {
  for (unsigned i = 0; i < Abbrev->getNumOperandInfos(); i++) {
    BitCodeAbbrevOp Op = Abbrev->getOperandInfo(i);
    if (Op.isEncoding()) {
      switch (Op.getEncoding()) {
      case BitCodeAbbrevOp::Fixed:
        // also used for backpatching
        if (Op.getEncodingData() >= 8)
          return true;
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
  return Result;
}

// jscpd:ignore-end

static std::shared_ptr<BitCodeAbbrev> MakeGeneralAbbrev() {
  auto Result = std::make_shared<BitCodeAbbrev>();
  Result->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  Result->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Result->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  return Result;
}

namespace {
class BitcodeAligner {
public:
  BitcodeAligner(MemoryBufferRef InBuffer, SmallVectorImpl<char> &OutBuffer);
  Error AlignBitcode();

private:
  MemoryBufferRef InBuffer;
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
  uint64_t ModuleInOffset = 0, ModuleOutOffset = 0;

  Error HandleStartBlock(unsigned ID);
  void HandleEndBlock();
  Error HandleBlockinfoBlock();
  void HandleDefineAbbrev();
  void HandleRecord(unsigned ID);
};
} // end anonymous namespace

BitcodeAligner::BitcodeAligner(MemoryBufferRef InBuffer,
                               SmallVectorImpl<char> &OutBuffer)
    : InBuffer(InBuffer), Reader(InBuffer), Writer(OutBuffer) {}

Error BitcodeAligner::HandleStartBlock(unsigned ID) {
  if (ID == bitc::IDENTIFICATION_BLOCK_ID) {
    // Keep track of offsets for multi-module files.
    ModuleInOffset = CurEntryInOffset - 32;
    ModuleOutOffset = CurEntryOutOffset - 32;
  } else if (ID == bitc::FUNCTION_BLOCK_ID) {
    OffsetMap[CurEntryInOffset - ModuleInOffset] =
        Writer.GetCurrentBitNo() - ModuleOutOffset;
  } else if (ID == bitc::VALUE_SYMTAB_BLOCK_ID && VSTOffsetPlaceholder) {
    if (VSTOffsetOldValue == (CurEntryInOffset - ModuleInOffset) / 32) {
      Writer.BackpatchWord(VSTOffsetPlaceholder,
                           (CurEntryOutOffset - ModuleOutOffset) / 32);
    }
  }
  if (Reader.EnterSubBlock(ID, nullptr))
    return error("Malformed block record");

  // Align the code width, and make it larger to accommodate the general
  // abbrev.
  if (Reader.getAbbrevIDWidth() >= 32)
    return error("Abbrev ID width too large");
  Writer.EnterSubblock(ID, alignTo<8>(Reader.getAbbrevIDWidth() + 1));

  Blocks.emplace_back();
  const auto *BI = BlockInfo.getBlockInfo(ID);
  Blocks.back().AbbrevIDMap.grow(bitc::FIRST_APPLICATION_ABBREV - 1);
  if (BI) {
    unsigned i = bitc::FIRST_APPLICATION_ABBREV;
    unsigned e = bitc::FIRST_APPLICATION_ABBREV + BI->Abbrevs.size();
    Blocks.back().AbbrevIDMap.grow(e - 1);
    unsigned j = i;
    for (; i != e; i++)
      if (AbbrevWorthKeeping(
              BI->Abbrevs[i - bitc::FIRST_APPLICATION_ABBREV].get()))
        Blocks.back().AbbrevIDMap[i] = j++;
  }

  Blocks.back().GeneralAbbrevID = Writer.EmitAbbrev(MakeGeneralAbbrev());
  return Error::success();
}

void BitcodeAligner::HandleEndBlock() {
  Writer.ExitBlock();
  Blocks.pop_back();
}

Error BitcodeAligner::HandleBlockinfoBlock() {
  Optional<BitstreamBlockInfo> NewBlockInfo = Reader.ReadBlockInfoBlock();
  if (!NewBlockInfo)
    return error("Malformed BlockInfoBlock");
  BlockInfo = std::move(*NewBlockInfo);

  Writer.EnterBlockInfoBlock();
  for (size_t i = 0, e = BlockInfo.getNumBlockInfos(); i != e; i++) {
    const BitstreamBlockInfo::BlockInfo *BI = BlockInfo.getBlockInfoByIndex(i);
    for (const auto &Abbrev : BI->Abbrevs)
      if (AbbrevWorthKeeping(Abbrev.get()))
        Writer.EmitBlockInfoAbbrev(BI->BlockID, AlignAbbrev(Abbrev.get()));
  }
  Writer.ExitBlock();
  return Error::success();
}

void BitcodeAligner::HandleDefineAbbrev() {
  unsigned InAbbrevID = Reader.ReadAbbrevRecord();
  const BitCodeAbbrev *Abbrev = Reader.getAbbrev(InAbbrevID);
  Blocks.back().AbbrevIDMap.grow(InAbbrevID);
  if (!AbbrevWorthKeeping(Abbrev))
    return;
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

Error BitcodeAligner::AlignBitcode() {
  // Skip the wrapper header, if any.
  const unsigned char *BufPtr =
      reinterpret_cast<const unsigned char *>(InBuffer.getBufferStart());
  const unsigned char *EndBufPtr =
      reinterpret_cast<const unsigned char *>(InBuffer.getBufferEnd());
  if (isBitcodeWrapper(BufPtr, EndBufPtr)) {
    if (SkipBitcodeWrapperHeader(BufPtr, EndBufPtr, true))
      return error("Invalid bitcode wrapper");
    Reader = BitstreamCursor(ArrayRef<uint8_t>(BufPtr, EndBufPtr));
  }
  if (!isRawBitcode(BufPtr, EndBufPtr))
    return error("Invalid magic bytes; not a bitcode file?");
  Reader.setBlockInfo(&BlockInfo);

  uint32_t Signature = Reader.Read(32);
  Writer.Emit(Signature, 32);

  while (!Reader.AtEndOfStream()) {
    CurEntryInOffset = Reader.GetCurrentBitNo();
    CurEntryOutOffset = Writer.GetCurrentBitNo();
    BitstreamEntry Entry =
        Reader.advance(BitstreamCursor::AF_DontAutoprocessAbbrevs);

    if (Entry.Kind == BitstreamEntry::SubBlock &&
        Entry.ID == bitc::BLOCKINFO_BLOCK_ID) {
      if (Error Err = HandleBlockinfoBlock())
        return Err;
    } else if (Entry.Kind == BitstreamEntry::SubBlock) {
      if (Error Err = HandleStartBlock(Entry.ID))
        return Err;
    } else if (Blocks.empty()) {
      return error("Invalid bitstream entry at top level");
    } else if (Entry.Kind == BitstreamEntry::EndBlock) {
      HandleEndBlock();
      // Skip padding at end of file, like llvm::getBitcodeFileContents.
      if (Blocks.empty())
        if (Reader.getCurrentByteNo() + 8 >= Reader.getBitcodeBytes().size())
          break;
    } else if (Entry.Kind == BitstreamEntry::Record &&
               Entry.ID == bitc::DEFINE_ABBREV) {
      HandleDefineAbbrev();
    } else if (Entry.Kind == BitstreamEntry::Record) {
      HandleRecord(Entry.ID);
    } else {
      return error("Malformed bitstream entry");
    }
  }

  if (!Blocks.empty())
    return error("Unexpected EOF");
  return Error::success();
}

Error bcdb::AlignBitcode(MemoryBufferRef InBuffer,
                         SmallVectorImpl<char> &OutBuffer) {
  return BitcodeAligner(InBuffer, OutBuffer).AlignBitcode();
}

void bcdb::WriteUnalignedModule(const Module &M,
                                SmallVectorImpl<char> &Buffer) {
  BitcodeWriter Writer(Buffer);
  Writer.writeModule(M);
  Writer.writeSymtab();
  Writer.writeStrtab();
}

void bcdb::WriteAlignedModule(const Module &M, SmallVectorImpl<char> &Buffer) {
  ExitOnError Err("WriteAlignedModule: ");

  SmallVector<char, 0> TmpBuffer;
  WriteUnalignedModule(M, TmpBuffer);
  Err(AlignBitcode(
      MemoryBufferRef(StringRef(TmpBuffer.data(), TmpBuffer.size()), ""),
      Buffer));
}

size_t bcdb::GetBitcodeSize(MemoryBufferRef Buffer) {
  ExitOnError Err("GetBitcodeSize: ");

  const unsigned char *BufPtr =
      reinterpret_cast<const unsigned char *>(Buffer.getBufferStart());
  const unsigned char *EndBufPtr =
      reinterpret_cast<const unsigned char *>(Buffer.getBufferEnd());
  if (isBitcodeWrapper(BufPtr, EndBufPtr)) {
    const unsigned char *FixedBufPtr = BufPtr;
    if (SkipBitcodeWrapperHeader(FixedBufPtr, EndBufPtr, true))
      report_fatal_error("Invalid bitcode wrapper");
    return EndBufPtr - BufPtr;
  }

  if (!isRawBitcode(BufPtr, EndBufPtr))
    report_fatal_error("Invalid magic bytes; not a bitcode file?");

  BitstreamCursor Reader(Buffer);
  Reader.Read(32); // skip signature
  while (true) {
    size_t EntryStart = Reader.getCurrentByteNo();
    BitstreamEntry Entry =
        Reader.advance(BitstreamCursor::AF_DontAutoprocessAbbrevs);
    if (Entry.Kind == BitstreamEntry::SubBlock) {
      if (Reader.SkipBlock())
        report_fatal_error("Invalid bitcode file");
    } else {
      // We must have reached the end of the module.
      return EntryStart;
    }
  }
}
