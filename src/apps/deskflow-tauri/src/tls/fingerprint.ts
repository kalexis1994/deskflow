// Port of deskflow::generateFingerprintArt and formatSSLFingerprintColumns
// (src/lib/net/SecureUtils.cpp), so the picture matches what the Qt GUI shows.

const POOL = " .o+=*BOX@%&#/^SE";

function bytes(hex: string): number[] {
  const clean = hex.replace(/[^0-9a-f]/gi, "");
  const out: number[] = [];
  for (let i = 0; i + 1 < clean.length; i += 2) out.push(parseInt(clean.slice(i, i + 2), 16));
  return out;
}

/** "Random art" of a digest: a worm walking a 17x9 field, two bits per step. */
export function fingerprintArt(hex: string): string {
  const base = 8;
  const rows = base + 1;
  const columns = base * 2 + 1;
  const len = POOL.length - 1;
  const field: number[][] = Array.from({ length: columns }, () => new Array(rows).fill(0));
  let x = Math.floor(columns / 2);
  let y = Math.floor(rows / 2);

  for (let byte of bytes(hex)) {
    for (let b = 0; b < 4; b++) {
      x += byte & 0x1 ? 1 : -1;
      y += byte & 0x2 ? 1 : -1;
      x = Math.min(Math.max(x, 0), columns - 1);
      y = Math.min(Math.max(y, 0), rows - 1);
      if (field[x][y] < len - 2) field[x][y]++;
      byte >>= 2;
    }
  }

  field[Math.floor(columns / 2)][Math.floor(rows / 2)] = len - 1;
  field[x][y] = len;

  const lines = ["╔═════════════════╗"];
  for (let r = 0; r < rows; r++) {
    let line = "║";
    for (let c = 0; c < columns; c++) line += POOL[Math.min(field[c][r], len)];
    lines.push(line + "║");
  }
  lines.push("╚═════════════════╝");
  return lines.join("\n");
}

/** `AB:CD:...` in rows of 24 characters, as the Qt GUI prints it. */
export function fingerprintColumns(hex: string): string {
  const text = bytes(hex)
    .map((b) => b.toString(16).padStart(2, "0").toUpperCase())
    .join(":");
  const rows: string[] = [];
  for (let i = 0; i < text.length; i += 24) rows.push(text.slice(i, i + 24));
  return rows.join("\n");
}

/** The art without its box-drawing frame, which fonts render unevenly; draw the frame with CSS. */
export function fingerprintArtBody(hex: string): string {
  return fingerprintArt(hex)
    .split("\n")
    .slice(1, -1)
    .map((line) => [...line].slice(1, -1).join(""))
    .join("\n");
}
