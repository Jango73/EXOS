#!/usr/bin/env node
// Generate an in-tree ASCII 8x16 font from an IBM VGA OTB bitmap font.

"use strict";

const fs = require("fs");
const path = require("path");

const DefaultFontPath = path.join(__dirname, "..", "..", "third", "fonts", "oldschool_pc_font_pack", "Bm437_IBM_VGA_8x16.otb");

function ReadU16BE(Buffer, Offset) {
    return Buffer.readUInt16BE(Offset);
}

function ReadU32BE(Buffer, Offset) {
    return Buffer.readUInt32BE(Offset);
}

function ReadI8(Buffer, Offset) {
    const Value = Buffer.readUInt8(Offset);
    return Value >= 0x80 ? Value - 0x100 : Value;
}

function ReadI16BE(Buffer, Offset) {
    const Value = Buffer.readUInt16BE(Offset);
    return Value >= 0x8000 ? Value - 0x10000 : Value;
}

function ParseTableDirectory(FontData) {
    const NumTables = ReadU16BE(FontData, 4);
    let Offset = 12;
    const Tables = {};

    for (let Index = 0; Index < NumTables; Index++) {
        const Tag = FontData.toString("ascii", Offset, Offset + 4);
        const TableOffset = ReadU32BE(FontData, Offset + 8);
        const Length = ReadU32BE(FontData, Offset + 12);
        Tables[Tag] = { Offset: TableOffset, Length };
        Offset += 16;
    }

    return Tables;
}

function SelectCmapSubtable(FontData, CmapOffset) {
    const NumTables = ReadU16BE(FontData, CmapOffset + 2);
    let Offset = CmapOffset + 4;
    let Selected = null;

    for (let Index = 0; Index < NumTables; Index++) {
        const PlatformId = ReadU16BE(FontData, Offset);
        const EncodingId = ReadU16BE(FontData, Offset + 2);
        const SubtableOffset = ReadU32BE(FontData, Offset + 4);
        const AbsoluteOffset = CmapOffset + SubtableOffset;
        const Format = ReadU16BE(FontData, AbsoluteOffset);

        const IsUnicode = (PlatformId === 0) || (PlatformId === 3 && (EncodingId === 0 || EncodingId === 1));
        if (!IsUnicode) {
            Offset += 8;
            continue;
        }

        const Candidate = {
            PlatformId,
            EncodingId,
            Format,
            Offset: AbsoluteOffset
        };

        if (!Selected) {
            Selected = Candidate;
        } else if (Selected.PlatformId !== 3 && PlatformId === 3) {
            Selected = Candidate;
        } else if (Selected.PlatformId === PlatformId && Selected.EncodingId !== 1 && EncodingId === 1) {
            Selected = Candidate;
        } else if (Selected.Format !== 4 && Format === 4) {
            Selected = Candidate;
        }

        Offset += 8;
    }

    if (!Selected) {
        throw new Error("No Unicode cmap subtable found.");
    }

    return Selected;
}

function BuildCmapFormat0(FontData, SubtableOffset) {
    const Map = new Array(256).fill(0);
    const GlyphArrayOffset = SubtableOffset + 6;
    for (let Codepoint = 0; Codepoint < 256; Codepoint++) {
        Map[Codepoint] = FontData.readUInt8(GlyphArrayOffset + Codepoint);
    }
    return Map;
}

function BuildCmapFormat4(FontData, SubtableOffset) {
    const SegCount = ReadU16BE(FontData, SubtableOffset + 6) / 2;
    const EndCodeOffset = SubtableOffset + 14;
    const StartCodeOffset = EndCodeOffset + (2 * SegCount) + 2;
    const IdDeltaOffset = StartCodeOffset + (2 * SegCount);
    const IdRangeOffsetOffset = IdDeltaOffset + (2 * SegCount);

    function MapCodepoint(Codepoint) {
        for (let Index = 0; Index < SegCount; Index++) {
            const EndCode = ReadU16BE(FontData, EndCodeOffset + (2 * Index));
            if (Codepoint > EndCode) {
                continue;
            }

            const StartCode = ReadU16BE(FontData, StartCodeOffset + (2 * Index));
            if (Codepoint < StartCode) {
                return 0;
            }

            const IdDelta = ReadI16BE(FontData, IdDeltaOffset + (2 * Index));
            const IdRangeOffset = ReadU16BE(FontData, IdRangeOffsetOffset + (2 * Index));

            if (IdRangeOffset === 0) {
                return (Codepoint + IdDelta) & 0xFFFF;
            }

            const GlyphOffset = IdRangeOffsetOffset + (2 * Index) + IdRangeOffset + (2 * (Codepoint - StartCode));
            if (GlyphOffset + 2 > FontData.length) {
                return 0;
            }

            const GlyphId = ReadU16BE(FontData, GlyphOffset);
            if (GlyphId === 0) {
                return 0;
            }
            return (GlyphId + IdDelta) & 0xFFFF;
        }
        return 0;
    }

    const Map = new Array(256).fill(0);
    for (let Codepoint = 0; Codepoint < 256; Codepoint++) {
        Map[Codepoint] = MapCodepoint(Codepoint);
    }
    return Map;
}

function BuildCodepointToGlyphMap(FontData, CmapOffset) {
    const Subtable = SelectCmapSubtable(FontData, CmapOffset);
    if (Subtable.Format === 0) {
        return BuildCmapFormat0(FontData, Subtable.Offset);
    }
    if (Subtable.Format === 4) {
        return BuildCmapFormat4(FontData, Subtable.Offset);
    }
    throw new Error(`Unsupported cmap format ${Subtable.Format}.`);
}

function ParseBitmapSizeTable(FontData, EblcOffset) {
    const NumSizes = ReadU32BE(FontData, EblcOffset + 4);
    const BaseOffset = EblcOffset + 8;

    for (let Index = 0; Index < NumSizes; Index++) {
        const SizeOffset = BaseOffset + (Index * 48);
        const IndexSubTableArrayOffset = ReadU32BE(FontData, SizeOffset);
        const NumberOfIndexSubTables = ReadU32BE(FontData, SizeOffset + 8);
        const StartGlyphIndex = ReadU16BE(FontData, SizeOffset + 40);
        const EndGlyphIndex = ReadU16BE(FontData, SizeOffset + 42);
        const PpemX = FontData.readUInt8(SizeOffset + 44);
        const PpemY = FontData.readUInt8(SizeOffset + 45);
        const BitDepth = FontData.readUInt8(SizeOffset + 46);

        if (PpemX === 16 && PpemY === 16 && BitDepth === 1) {
            return {
                IndexSubTableArrayOffset,
                NumberOfIndexSubTables,
                StartGlyphIndex,
                EndGlyphIndex
            };
        }
    }

    throw new Error("No 8x16 bitmap size table found.");
}

function ParseIndexSubTables(FontData, EblcOffset, SizeTable) {
    const Entries = [];
    let ArrayOffset = EblcOffset + SizeTable.IndexSubTableArrayOffset;

    for (let Index = 0; Index < SizeTable.NumberOfIndexSubTables; Index++) {
        const FirstGlyph = ReadU16BE(FontData, ArrayOffset);
        const LastGlyph = ReadU16BE(FontData, ArrayOffset + 2);
        const AdditionalOffset = ReadU32BE(FontData, ArrayOffset + 4);
        const SubTableOffset = EblcOffset + SizeTable.IndexSubTableArrayOffset + AdditionalOffset;
        const IndexFormat = ReadU16BE(FontData, SubTableOffset);
        const ImageFormat = ReadU16BE(FontData, SubTableOffset + 2);
        const ImageDataOffset = ReadU32BE(FontData, SubTableOffset + 4);

        const Entry = {
            FirstGlyph,
            LastGlyph,
            IndexFormat,
            ImageFormat,
            ImageDataOffset
        };

        if (IndexFormat === 1) {
            const GlyphCount = LastGlyph - FirstGlyph + 1;
            const OffsetArray = [];
            let Offset = SubTableOffset + 8;
            for (let OffsetIndex = 0; OffsetIndex < GlyphCount + 1; OffsetIndex++) {
                OffsetArray.push(ReadU32BE(FontData, Offset));
                Offset += 4;
            }
            Entry.OffsetArray = OffsetArray;
        } else if (IndexFormat === 2) {
            Entry.ImageSize = ReadU32BE(FontData, SubTableOffset + 8);
        } else {
            throw new Error(`Unsupported index format ${IndexFormat}.`);
        }

        Entries.push(Entry);
        ArrayOffset += 8;
    }

    return Entries;
}

function ReadGlyphData(FontData, EbdtOffset, SubTable, GlyphIndex) {
    if (GlyphIndex < SubTable.FirstGlyph || GlyphIndex > SubTable.LastGlyph) {
        return null;
    }

    let Offset = 0;
    let Length = 0;
    if (SubTable.IndexFormat === 1) {
        const Index = GlyphIndex - SubTable.FirstGlyph;
        const StartOffset = SubTable.OffsetArray[Index];
        const EndOffset = SubTable.OffsetArray[Index + 1];
        Offset = StartOffset;
        Length = EndOffset - StartOffset;
    } else if (SubTable.IndexFormat === 2) {
        Offset = (GlyphIndex - SubTable.FirstGlyph) * SubTable.ImageSize;
        Length = SubTable.ImageSize;
    }

    const GlyphOffset = EbdtOffset + SubTable.ImageDataOffset + Offset;
    return FontData.slice(GlyphOffset, GlyphOffset + Length);
}

function DecodeFormat2(BitmapData) {
    if (BitmapData.length < 6) {
        return null;
    }

    const Height = BitmapData.readUInt8(0);
    const Width = BitmapData.readUInt8(1);
    const BearingX = ReadI8(BitmapData, 2);
    const BearingY = ReadI8(BitmapData, 3);
    const Advance = BitmapData.readUInt8(4);
    const BitData = BitmapData.slice(5);

    if (Width > 8 || Height > 16) {
        return null;
    }

    const TotalBits = Width * Height;
    if ((BitData.length * 8) < TotalBits) {
        return null;
    }

    const Rows = new Array(16).fill(0);
    for (let BitIndex = 0; BitIndex < TotalBits; BitIndex++) {
        const ByteIndex = Math.floor(BitIndex / 8);
        const BitInByte = 7 - (BitIndex % 8);
        const BitValue = (BitData[ByteIndex] >> BitInByte) & 1;
        if (BitValue === 0) {
            continue;
        }

        const X = BitIndex % Width;
        const Y = Math.floor(BitIndex / Width);
        const RowIndex = Y;
        const ColumnIndex = X;
        Rows[RowIndex] |= (1 << (7 - ColumnIndex));
    }

    return Rows;
}

function DecodeFormat5(BitmapData) {
    if (BitmapData.length < 16) {
        return null;
    }
    return Array.from(BitmapData.slice(0, 16));
}

function ExtractGlyphRows(FontData, EbdtOffset, SubTables, GlyphIndex) {
    for (const SubTable of SubTables) {
        const Data = ReadGlyphData(FontData, EbdtOffset, SubTable, GlyphIndex);
        if (!Data) {
            continue;
        }
        if (SubTable.ImageFormat === 5) {
            return DecodeFormat5(Data);
        }
        if (SubTable.ImageFormat === 2) {
            const Rows = DecodeFormat2(Data);
            if (Rows) {
                return Rows;
            }
        }
        return null;
    }
    return null;
}

function BuildOutput(GlyphBytes) {
    const Out = [];

    Out.push("// Auto-generated from scripts/utils/gen-font-ascii.js. Do not edit manually.");
    Out.push("// Source: third/fonts/oldschool_pc_font_pack/Bm437_IBM_VGA_8x16.otb");
    Out.push("// License: Creative Commons Attribution-ShareAlike 4.0 International.");
    Out.push("");
    Out.push("#include \"Font.h\"");
    Out.push("");
    Out.push("/************************************************************************/");
    Out.push("");
    Out.push(`static const U8 FontAsciiGlyphs[${GlyphBytes.length}] = {`);
    for (let Index = 0; Index < GlyphBytes.length; Index += 16) {
        const Line = GlyphBytes.slice(Index, Index + 16)
            .map(Value => "0x" + Value.toString(16).padStart(2, "0").toUpperCase())
            .join(", ");
        Out.push("    " + Line + (Index + 16 < GlyphBytes.length ? "," : ""));
    }
    Out.push("};");
    Out.push("");
    Out.push("const FONT_GLYPH_SET FontAscii = {");
    Out.push("    .Width = 8u,");
    Out.push("    .Height = 16u,");
    Out.push("    .BytesPerRow = 1u,");
    Out.push("    .GlyphCount = 256u,");
    Out.push("    .GlyphData = FontAsciiGlyphs");
    Out.push("};");
    Out.push("");
    Out.push("/************************************************************************/");
    Out.push("");

    return Out.join("\n");
}

function Main() {
    const FontPath = process.argv[2] || DefaultFontPath;
    if (!fs.existsSync(FontPath)) {
        throw new Error(`Font file not found: ${FontPath}`);
    }

    const FontData = fs.readFileSync(FontPath);
    const Tables = ParseTableDirectory(FontData);
    if (!Tables.EBLC || !Tables.EBDT || !Tables.cmap) {
        throw new Error("Font file is missing EBLC/EBDT/cmap tables.");
    }

    const SizeTable = ParseBitmapSizeTable(FontData, Tables.EBLC.Offset);
    const SubTables = ParseIndexSubTables(FontData, Tables.EBLC.Offset, SizeTable);
    const CodepointToGlyph = BuildCodepointToGlyphMap(FontData, Tables.cmap.Offset);

    const GlyphBytes = [];
    for (let Codepoint = 0; Codepoint < 256; Codepoint++) {
        const GlyphIndex = CodepointToGlyph[Codepoint];
        const Rows = ExtractGlyphRows(FontData, Tables.EBDT.Offset, SubTables, GlyphIndex);
        if (!Rows || Rows.length !== 16) {
            GlyphBytes.push(...new Array(16).fill(0));
            continue;
        }
        GlyphBytes.push(...Rows);
    }

    const Output = BuildOutput(GlyphBytes);
    const OutputPath = path.join(__dirname, "..", "..", "kernel", "source", "font", "FontData-ASCII.c");
    fs.writeFileSync(OutputPath, Output);
}

Main();
