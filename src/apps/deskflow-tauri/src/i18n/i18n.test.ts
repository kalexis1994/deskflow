import { afterEach, describe, expect, it } from "vitest";
import { DICTIONARIES, LANGUAGES, resolveLanguage, setLanguage, t, tn } from "./index";

const placeholders = (text: string) => [...text.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort();
/** `key_one`, `key_other`… count as `key`, since each language needs different forms. */
const base = (key: string) => key.replace(/_(zero|one|two|few|many|other)$/, "");
const plurals = (dict: Record<string, string>) => new Set(Object.keys(dict).filter((k) => /_other$/.test(k)).map(base));

describe("translations", () => {
  afterEach(() => setLanguage("es"));

  const reference = DICTIONARIES.es;

  for (const { code } of LANGUAGES.filter((l) => l.code !== "es")) {
    it(`${code} has every text and nothing else`, () => {
      const dict = DICTIONARIES[code] ?? {};
      expect([...new Set(Object.keys(dict).map(base))].sort()).toEqual([...new Set(Object.keys(reference).map(base))].sort());
      expect([...plurals(dict)].sort()).toEqual([...plurals(reference)].sort());
    });

    it(`${code} keeps the placeholders`, () => {
      const dict = DICTIONARIES[code] ?? {};
      for (const [key, text] of Object.entries(dict)) {
        const ref = reference[key] ?? reference[`${base(key)}_other`];
        expect(placeholders(text), `${code} ${key}`).toEqual(placeholders(ref));
      }
    });
  }

  it("leaves no text empty", () => {
    for (const [lang, dict] of Object.entries(DICTIONARIES)) {
      for (const [key, text] of Object.entries(dict)) expect(text.trim(), `${lang} ${key}`).not.toBe("");
    }
  });

  it("follows the system's language when there is no choice", () => {
    expect(resolveLanguage("auto", ["fr-FR", "en-US"])).toBe("fr");
    expect(resolveLanguage("auto", ["ja-JP", "pt-BR"])).toBe("pt");
    expect(resolveLanguage("auto", ["ja-JP"])).toBe("en");
    expect(resolveLanguage("de", ["fr-FR"])).toBe("de");
    expect(resolveLanguage("klingon", [])).toBe("en");
  });

  it("fills placeholders and plurals in the current language", () => {
    setLanguage("en");
    expect(t("common.tab.home")).toBe("Home");
    expect(tn("common.warnings", 1)).toBe("1 warning since it started");
    expect(tn("common.warnings", 3)).toBe("3 warnings since it started");
    setLanguage("es");
    expect(tn("common.warnings", 3)).toBe("3 avisos desde que arrancó");
  });
});
