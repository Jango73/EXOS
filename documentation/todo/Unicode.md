# Specification: Minimal Unicode Module

## 1) Goals
- **Single encoding for storage/transport**: strict UTF-8.  
- **Safe manipulation**: decode/encode Unicode scalar values (U+0000..U+10FFFF, excluding surrogates).  
- **Basic normalization**: NFC (precomposed form) for Western Latin accents (French, European). Leave other scripts untouched.  
- **UI-safe slicing**: iterate by **grapheme cluster** (subset of UAX #29) so you don’t break characters visually.  

Out of scope v1: full collation, full normalization for all scripts, full word/line segmentation.

---

## 2) Terminology & Invariants
- **Byte**: 8-bit unit from UTF-8 stream.  
- **Code point** (cp): integer ∈ [0..0x10FFFF]\{0xD800..0xDFFF}.  
- **Grapheme cluster**: sequence of one or more code points seen by the user as “a single character”.  
- **Valid string**: UTF-8 with no overlongs, no surrogates, ≤ U+10FFFF.  
- **E/S immutability**: functions never modify input buffers; lengths are in **bytes** unless explicitly code points.  

---

## 3) Interfaces (conceptual signatures)

### 3.1 UTF-8
- `DecodeUtf8(p: byte*, end: byte*, out cp: u32, out adv: usize)`  
  → returns `Ok`, `Truncated`, or `Invalid`. `adv` = bytes consumed (0 if truncated).  
- `EncodeUtf8(cp: u32, out buf[4]: byte, out len: usize)`  
  → `Invalid` if cp ∉ valid Unicode scalars.  
- `ValidateUtf8(data: byte*, len: usize, out cpCount: usize)`  
  → fails at the **first** invalid byte.  

### 3.2 Normalization (NFC Basic)
- `NfcBasic_Cp(inCp: u32[], inCount: usize, outCp: u32[], outCap: usize, outCount: usize)`  
  → collapse base+accent into precomposed Latin letters where known.  
- `NfcBasic_Utf8(inUtf8: byte*, inLen: usize, outUtf8: byte*, outCap: usize, outLen: usize)`  
  → `OutOfSpace` if not enough output buffer.  

### 3.3 Graphemes
- `InitGrapheme(data: byte*, len: usize)`  
- `NextGrapheme(iter: inout GraphemeIter, out start: byte*, out clen: usize)`  
  → advance by visible cluster.  

### 3.4 Helpers (classification)
- `IsCombining(cp: u32)`  
- `IsZwj(cp: u32)` (U+200D)  
- `IsVariationSelector(cp: u32)` (U+FE00..U+FE0F)  
- `IsSkinTone(cp: u32)` (U+1F3FB..U+1F3FF)  
- `IsRegionalIndicator(cp: u32)` (U+1F1E6..U+1F1FF)  
- `IsExtendedPictographic(cp: u32)`  

---

## 4) UTF-8 Validation Rules
- **Disallowed**: overlongs, surrogates, > U+10FFFF.  
- **Byte length table**:  
  - `0xxxxxxx` → 1 byte  
  - `110xxxxx 10xxxxxx` → 2 bytes (≥ U+0080)  
  - `1110xxxx 10xxxxxx 10xxxxxx` → 3 bytes (≥ U+0800, not surrogates)  
  - `11110xxx 10xxxxxx 10xxxxxx 10xxxxxx` → 4 bytes (≥ U+10000, ≤ U+10FFFF)  
- On invalid byte → `Invalid` with `adv=1`.  
- On premature end → `Truncated` with `adv=0`.  

---

## 5) NFC Basic Normalization

### 5.1 Goal
- Avoid duplicates like `é` precomposed (U+00E9) vs `e` + U+0301.  
- Cover Western Europe: A/E/I/O/U/Y/C/N with accents (acute, grave, circumflex, tilde, diaeresis, cedilla).  

### 5.2 Pipeline
1. Decode UTF-8 → cp[].  
2. Canonical reorder combining marks (0300..036F etc.).  
3. Compose with a small lookup table (~200 entries).  
4. Emit normalized cp[].  

### 5.3 Properties
- Idempotent: `NfcBasic(NfcBasic(x)) == NfcBasic(x)`.  
- Stable: never generate invalid cp.  

---

## 6) Grapheme Clusters (subset of UAX #29)

Rules (simplified):  
- CR+LF = 1 cluster.  
- Break around CR/LF/Control.  
- Extend: base + combining marks/ZWJ/VS stay in cluster.  
- Emoji skin tones join with base.  
- ZWJ sequences (👩+ZWJ+👩+ZWJ+👧+ZWJ+👦) = 1 cluster.  
- Regional Indicators: pairs (🇫 + 🇷) = 1 cluster; break after each pair.  
- Else: break between scalars.  

Guarantee: no broken emoji, no broken diacritics, flags handled.  

---

## 7) Status Codes
- `Ok`, `Invalid`, `Truncated`, `OutOfSpace`.  
- Contract: functions consuming invalid input must still **progress** (no infinite loops).  

---

## 8) Concurrency / Portability
- Thread-safe (no global state).  
- Endianness irrelevant (UTF-8).  
- Target: C99 portable.  

---

## 9) Performance Targets
- ASCII-majority decode ≥ 1.5 GB/s.  
- Mixed text ≥ 0.8 GB/s.  
- Grapheme iteration overhead ≤ 20%.  

---

## 10) Security
- Strict reject of overlongs/surrogates.  
- Always bound-check before reading next byte.  
- Allow caller to substitute U+FFFD replacement if desired.  

---

## 11) Mandatory Tests

### Validation
- ASCII `"Hello"` → Ok, cpCount=5.  
- Overlongs → Invalid.  
- Surrogate → Invalid.  
- U+10FFFF → Ok.  
- U+110000 → Invalid.  

### Normalization
- `"e" + U+0301` → `"é"`.  
- `"c" + U+0327` → `"ç"`.  
- Unsupported scripts → unchanged.  

### Graphemes
- `"e" + U+0301` = 1 cluster.  
- Family emoji = 1 cluster.  
- Flag 🇫🇷 = 1 cluster; 🇫🇷🇺 = 2 clusters.  
- CRLF = 1 cluster.  

---

## 12) Tables / Data Size
- NFC compose table: ~3 KB.  
- Combining mark ranges: compact table.  
- Extended pictographic check: ≤ 1 KB.  

---

## 13) Extensions (future)
- Full NFD/NFKC/NFKD.  
- Full collation (CLDR).  
- Full UAX #29 word/line segmentation.  
- Line breaking (UAX #14).  

---

## 14) Acceptance Criteria
- Pass all tests.  
- No UB (undefined behavior).  
- No malloc / out-of-bounds.  
- API names + semantics stable.  

---

# Glossary

- **Unicode**: global standard mapping every character/symbol to a numeric code point.  
- **Code point**: the unique number assigned to a character (e.g., U+0041 = "A").  
- **Scalar value**: a code point that is valid (not a surrogate).  
- **UTF-8**: a way to encode code points into 1–4 bytes.  
- **Surrogate**: reserved range (U+D800..U+DFFF), only used in UTF-16, invalid in UTF-8.  
- **Overlong sequence**: illegal UTF-8 where a simple character is encoded with too many bytes.  
- **Normalization (NFC)**: process of making equivalent characters have the same binary representation (e.g., `é` vs `e+´`). NFC = Normalization Form Composed.  
- **Combining mark**: a code point that modifies the previous one (accents, diacritics).  
- **Grapheme cluster**: what users see as one character, possibly made from several code points (e.g., 👩‍👩‍👧‍👦).  
- **ZWJ (Zero-Width Joiner)**: special code point (U+200D) that “glues” emojis/letters into one visible glyph.  
- **Variation selector**: code points (FE00–FE0F) that choose alternative glyph forms (e.g., emoji vs text).  
- **Regional indicator**: code points (U+1F1E6–U+1F1FF) used in pairs to form flags.  
- **Extended pictographic**: Unicode class covering emoji bases (faces, animals, symbols).  
- **Idempotent**: applying an operation twice gives the same result as once.  
- **Collation**: locale-dependent sorting/comparison rules.  
- **UAX #29**: Unicode Annex defining text segmentation (grapheme/word/line breaks).  
