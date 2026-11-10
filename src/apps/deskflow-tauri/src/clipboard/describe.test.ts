import { describe, expect, it } from "vitest";
import {
  contentKind,
  describeActivity,
  describeProgress,
  filesFacts,
  filesPhrase,
  formatSize,
  relativeTime,
  type ClipboardActivity,
  type FilesSummary,
} from "./describe";

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
    expect(contentKind(["png"])).toEqual({ kind: "image", phrase: "una imagen" });
    expect(contentKind(["text", "png"]).kind).toBe("mixed");
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

  it("counts a picture left out as bitmap and as PNG once", () => {
    const v = describeActivity(
      { ...base, event: "shared", computer: "laptop", formats: ["text"], dropped: ["bitmap", "png"], image: null },
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

const fiveFiles: FilesSummary = { count: 5, folders: 1, bytes: 3502, names: ["fotos", "informe.pdf", "c.txt"], roots: 4 };
const filesBase: ClipboardActivity = {
  ...base,
  formats: ["files"],
  size: 3502,
  chars: null,
  lines: null,
  text: null,
  files: fiveFiles,
};

describe("files", () => {
  it("names a copy by its only item or by how many files", () => {
    expect(filesPhrase({ count: 1, folders: 0, bytes: 9, names: ["informe.pdf"], roots: 1 })).toBe("«informe.pdf»");
    expect(filesPhrase({ count: 12, folders: 3, bytes: 9, names: ["fotos"], roots: 1 })).toBe("«fotos»");
    expect(filesPhrase(fiveFiles)).toBe("5 archivos");
    expect(filesPhrase({ count: 0, folders: 1, bytes: 0, names: ["a", "b"], roots: 2 })).toBe("una carpeta vacía");
    expect(filesPhrase(null)).toBe("archivos");
  });

  it("lists counts, a few names and the size", () => {
    expect(filesFacts(fiveFiles)).toEqual(["5 archivos · 1 carpeta", "fotos, informe.pdf, c.txt y 1 más", "3 KB"]);
    expect(filesFacts({ count: 1, folders: 0, bytes: 10, names: ["a.txt"], roots: 1 })).toEqual(["1 archivo", "10 B"]);
  });

  it("tells where the files went", () => {
    const fromClient = describeActivity(filesBase, "client", "pc", 3);
    expect(fromClient.title).toBe("Copiaste 5 archivos en esta computadora");
    expect(fromClient.kind).toBe("files");
    expect(fromClient.facts).toContain("3 KB");

    const toLaptop = describeActivity({ ...filesBase, computer: "laptop" }, "server", "escritorio", 3);
    expect(toLaptop.title).toBe("Se enviaron 5 archivos a laptop");

    const arrived = describeActivity({ ...filesBase, event: "received" }, "client", "pc", 3);
    expect(arrived.title).toBe("Llegaron 5 archivos");
    expect(arrived.detail).toMatch(/Ctrl\+V/);

    const shared = describeActivity({ ...filesBase, event: "shared", computer: "laptop" }, "server", "escritorio", 3);
    expect(shared.title).toBe("laptop copió 5 archivos");
  });

  it("explains failures in Spanish, with the English detail when nothing fits", () => {
    const noUser = describeActivity({ ...filesBase, event: "failed", reason: "noUser", files: null, detail: "x" }, "client", "pc", 3);
    expect(noUser.title).toBe("No se pudieron copiar los archivos");
    expect(noUser.detail).toMatch(/sesión iniciada/);
    expect(noUser.tone).toBe("warn");

    const damaged = describeActivity({ ...filesBase, event: "failed", reason: "damaged", computer: "laptop" }, "server", "escritorio", 3);
    expect(damaged.title).toBe("No se pudieron copiar 5 archivos con laptop");
    expect(damaged.tone).toBe("danger");

    const other = describeActivity({ ...filesBase, event: "failed", reason: null, detail: "a.txt: can't be opened" }, "client", "pc", 3);
    expect(other.detail).toBe("Detalle: a.txt: can't be opened");

    const tooLarge = describeActivity({ ...filesBase, event: "skipped", reason: "tooLarge", files: null }, "client", "pc", 3, 500);
    expect(tooLarge.detail).toMatch(/500 MB/);
  });

  it("shows the progress of a copy on its way", () => {
    expect(describeProgress({ direction: "send", computer: "laptop", count: 3, bytes: 4 * 1024 * 1024, done: 1024 * 1024 })).toEqual({
      title: "Enviando 3 archivos a laptop",
      detail: "1 MB de 4 MB",
      percent: 25,
    });
    expect(describeProgress({ direction: "send", computer: null, count: 1, bytes: 10, done: 3 }).title).toBe("Enviando 1 archivo al servidor");
    expect(describeProgress({ direction: "receive", computer: null, count: 2, bytes: 0, done: 0 })).toMatchObject({
      title: "Recibiendo 2 archivos",
      percent: 100,
    });
  });
});
