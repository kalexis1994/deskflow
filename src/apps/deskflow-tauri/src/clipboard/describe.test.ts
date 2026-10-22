import { describe, expect, it } from "vitest";
import { contentKind, describeActivity, formatSize, relativeTime, type ClipboardActivity } from "./describe";

const base: ClipboardActivity = {
  event: "sent",
  computer: null,
  reason: null,
  formats: ["text"],
  dropped: [],
  size: 64,
  chars: 18,
  lines: 2,
  image: null,
  text: "hola\nmundo",
  at: 0,
};

describe("contentKind", () => {
  it("names what was copied", () => {
    expect(contentKind(["text"]).phrase).toBe("texto");
    expect(contentKind(["text", "HTML"]).phrase).toBe("texto con formato");
    expect(contentKind(["HTML"]).phrase).toBe("texto con formato");
    expect(contentKind(["bitmap"])).toEqual({ kind: "image", phrase: "una imagen" });
    expect(contentKind(["text", "bitmap"]).kind).toBe("mixed");
    expect(contentKind([]).kind).toBe("none");
  });
});

describe("describeActivity", () => {
  it("a copy on this client went to the server", () => {
    const v = describeActivity(base, "client", "pc", 3);
    expect(v.title).toBe("Copiaste texto en esta computadora");
    expect(v.detail).toMatch(/servidor/);
    expect(v.facts).toEqual(["18 caracteres · 2 líneas", "64 B"]);
    expect(v.tone).toBe("ok");
  });

  it("the server shows who copied", () => {
    const fromLaptop = describeActivity({ ...base, event: "shared", computer: "laptop" }, "server", "escritorio", 3);
    expect(fromLaptop.title).toBe("laptop copió texto");
    const fromHere = describeActivity({ ...base, event: "shared", computer: "escritorio" }, "server", "escritorio", 3);
    expect(fromHere.title).toBe("Copiaste texto en esta computadora");
  });

  it("explains what was left out to fit", () => {
    const v = describeActivity(
      { ...base, event: "shared", computer: "laptop", formats: ["text"], dropped: ["bitmap"], image: null },
      "server",
      "escritorio",
      3,
    );
    expect(v.note).toBe("Se compartió sin la imagen: no entraba en el límite de 3 MB.");
  });

  it("images show their size", () => {
    const v = describeActivity(
      { ...base, event: "received", formats: ["bitmap"], chars: null, lines: null, text: null, image: { width: 1920, height: 1080 }, size: 8_294_454 },
      "client",
      "pc",
      10,
    );
    expect(v.title).toBe("Llegó una imagen de otra computadora");
    expect(v.facts).toEqual(["1920 × 1080", "7,9 MB"]);
  });

  it("says why nothing was shared", () => {
    const tooLarge = describeActivity({ ...base, event: "skipped", reason: "tooLarge" }, "server", "x", 3);
    expect(tooLarge.tone).toBe("warn");
    expect(tooLarge.detail).toMatch(/Opciones del servidor/);
    expect(describeActivity({ ...base, event: "skipped", reason: "tooLarge" }, "client", "x", 3).detail).not.toMatch(/Opciones/);

    const files = describeActivity({ ...base, event: "skipped", reason: "nothingToShare", formats: [], size: null }, "client", "x", 3);
    expect(files.detail).toMatch(/archivos/);

    const busy = describeActivity({ ...base, event: "failed", reason: "busy" }, "client", "x", 3);
    expect(busy.tone).toBe("danger");
  });
});

describe("formatting", () => {
  it("sizes", () => {
    expect(formatSize(512)).toBe("512 B");
    expect(formatSize(3 * 1024)).toBe("3 KB");
    expect(formatSize(3 * 1024 * 1024)).toBe("3 MB");
  });

  it("relative times", () => {
    expect(relativeTime(0, 3_000)).toBe("hace un momento");
    expect(relativeTime(0, 30_000)).toBe("hace 30 s");
    expect(relativeTime(0, 5 * 60_000)).toBe("hace 5 min");
    expect(relativeTime(0, 3 * 3_600_000)).toBe("hace 3 h");
  });
});
