#!/usr/bin/env python3
# Build RTL Persian HTML docs from the markdown sources in docs/.
# Usage: python3 docs/build_html.py   (run from the library root or anywhere)
import html
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
OUT = DOCS / "html"

CSS = """
:root { --bg:#0f1420; --panel:#171e2e; --ink:#e8ecf4; --dim:#9aa7bd;
        --accent:#4db2ff; --accent2:#7ee0a3; --warn:#ffcc66; --code:#0b101a; }
* { box-sizing: border-box; }
body { direction: rtl; margin:0; background:var(--bg); color:var(--ink);
       font-family: Vazirmatn, "Segoe UI", Tahoma, Arial, sans-serif;
       line-height: 2; }
main { max-width: 62rem; margin: 0 auto; padding: 2rem 1.4rem 6rem; }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
h1 { font-size: 1.9rem; border-bottom: 3px solid var(--accent);
     padding-bottom: .5rem; color: #fff; }
h2 { font-size: 1.45rem; margin-top: 2.6rem; color: var(--accent2);
     border-right: 6px solid var(--accent2); padding-right: .8rem; }
h3 { font-size: 1.15rem; margin-top: 1.8rem; color: var(--warn); }
p  { margin: .9rem 0; }
code { direction: ltr; display: inline-block; background: var(--code);
       color: #9fe8ff; padding: .05rem .4rem; border-radius: .35rem;
       font-family: "JetBrains Mono", Consolas, monospace; font-size: .88em; }
pre { direction: ltr; text-align: left; background: var(--code);
      border: 1px solid #26314a; border-radius: .6rem; padding: 1rem 1.2rem;
      overflow-x: auto; line-height: 1.7; }
pre code { background: none; padding: 0; color: #cfe6ff; }
blockquote { border-right: 4px solid var(--warn); background: #1c2333;
             margin: 1rem 0; padding: .6rem 1.1rem; border-radius: .5rem;
             color: #dbe4f2; }
table { border-collapse: collapse; width: 100%; margin: 1.2rem 0; }
th, td { border: 1px solid #2b3752; padding: .55rem .8rem; text-align: right; }
th { background: #1d2740; color: var(--accent2); }
tr:nth-child(even) td { background: #141b2b; }
ul, ol { padding-right: 1.6rem; }
li { margin: .35rem 0; }
hr { border: none; border-top: 1px solid #2b3752; margin: 2.2rem 0; }
nav.top { background: var(--panel); border-bottom: 1px solid #2b3752;
          padding: .9rem 1.4rem; display: flex; gap: 1.2rem;
          justify-content: center; flex-wrap: wrap; position: sticky; top: 0; }
nav.top a { color: var(--dim); font-size: .95rem; }
nav.top a:hover { color: var(--accent); }
.badge { display:inline-block; background:#22304e; color:var(--accent2);
         border-radius:.8rem; padding:.05rem .7rem; font-size:.8rem;
         margin-left:.4rem; }
footer { text-align:center; color:var(--dim); font-size:.85rem;
         padding:2rem 0 3rem; }
"""


def inline(s: str) -> str:
    s = html.escape(s, quote=False)
    s = re.sub(r"`([^`]+)`", r"<code>\1</code>", s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", s)
    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', s)
    return s


def md_to_html(md: str) -> str:
    lines = md.splitlines()
    out, i, n = [], 0, len(lines)
    in_ul = in_ol = False

    def close_lists():
        nonlocal in_ul, in_ol
        if in_ul:
            out.append("</ul>")
            in_ul = False
        if in_ol:
            out.append("</ol>")
            in_ol = False

    while i < n:
        ln = lines[i]
        stripped = ln.strip()

        if stripped.startswith("```"):
            close_lists()
            lang = stripped[3:].strip()
            buf = []
            i += 1
            while i < n and not lines[i].strip().startswith("```"):
                buf.append(lines[i])
                i += 1
            i += 1
            cls = f' class="lang-{html.escape(lang)}"' if lang else ""
            out.append(f"<pre><code{cls}>" + html.escape("\n".join(buf)) + "</code></pre>")
            continue

        if stripped.startswith("|") and i + 1 < n and re.match(r"^\|[\s:|-]+\|$", lines[i + 1].strip()):
            close_lists()
            header = [c.strip() for c in stripped.strip("|").split("|")]
            i += 2
            rows = []
            while i < n and lines[i].strip().startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            out.append("<table><thead><tr>" + "".join(f"<th>{inline(c)}</th>" for c in header) +
                       "</tr></thead><tbody>")
            for r in rows:
                out.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>")
            out.append("</tbody></table>")
            continue

        if stripped.startswith("###"):
            close_lists()
            out.append(f"<h3>{inline(stripped[3:].strip())}</h3>")
        elif stripped.startswith("##"):
            close_lists()
            out.append(f"<h2>{inline(stripped[2:].strip())}</h2>")
        elif stripped.startswith("#"):
            close_lists()
            out.append(f"<h1>{inline(stripped[1:].strip())}</h1>")
        elif stripped.startswith("---"):
            close_lists()
            out.append("<hr>")
        elif stripped.startswith(">"):
            close_lists()
            out.append(f"<blockquote><p>{inline(stripped[1:].strip())}</p></blockquote>")
        elif re.match(r"^[-*] ", stripped):
            if not in_ul:
                close_lists()
                out.append("<ul>")
                in_ul = True
            out.append(f"<li>{inline(stripped[2:].strip())}</li>")
        elif re.match(r"^\d+\. ", stripped):
            if not in_ol:
                close_lists()
                out.append("<ol>")
                in_ol = True
            out.append(f"<li>{inline(re.sub(r'^[0-9]+[.] ', '', stripped))}</li>")
        elif stripped == "":
            close_lists()
        else:
            close_lists()
            out.append(f"<p>{inline(stripped)}</p>")
        i += 1

    close_lists()
    return "\n".join(out)


def page(title: str, body: str, home: bool = False) -> str:
    nav = ('<nav class="top">'
           '<a href="index.html">🏠 صفحه اصلی</a>'
           '<a href="guide-architecture.html">معماری</a>'
           '<a href="guide-call_management.html">مدیریت تماس</a>'
           '<a href="guide-sms_policy.html">سیاست پیامک</a>'
           '<a href="guide-watchdog.html">واچ‌داگ‌ها</a>'
           '<a href="guide-cli.html">کنسول CLI</a>'
           '<a href="files-index.html">فایل‌به‌فایل</a>'
           '</nav>')
    foot = '<footer>مستندات کتابخانه GSM — نسخه ۰.۴ · تولیدشده با build_html.py</footer>'
    return (f"<!doctype html><html lang=\"fa\" dir=\"rtl\"><head>"
            f"<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            f"<title>{html.escape(title)}</title><style>{CSS}</style></head><body>"
            f"{nav}<main>{body}</main>{foot}</body></html>")


GUIDES = {
    "README.md": ("index", "مستندات کتابخانه GSM"),
    "ARCHITECTURE.md": ("guide-architecture", "معماری سیستم"),
    "COMPREHENSIVE_GUIDE.md": ("guide-comprehensive", "راهنمای جامع"),
    "CALL_MANAGEMENT.md": ("guide-call_management", "مدیریت تماس"),
    "SMS_POLICY.md": ("guide-sms_policy", "سیاست پیامک"),
    "WATCHDOG.md": ("guide-watchdog", "واچ‌داگ‌ها"),
    "CLI.md": ("guide-cli", "کنسول CLI"),
    "QUIRKS.md": ("guide-quirks", "نکات عملی"),
    "INDUSTRIAL_RECOVERY.md": ("guide-industrial", "بازیابی صنعتی"),
}


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    generated = {}

    for fname, (slug, title) in GUIDES.items():
        src = DOCS / fname
        if not src.exists():
            print(f"skip (missing): {fname}")
            continue
        body = md_to_html(src.read_text(encoding="utf-8"))
        (OUT / f"{slug}.html").write_text(page(title, body), encoding="utf-8")
        generated[fname] = slug
        print(f"ok: {fname} -> {slug}.html")

    files_dir = DOCS / "fa" / "files"
    entries = []
    if files_dir.exists():
        for src in sorted(files_dir.glob("*.md")):
            if src.name.startswith("INDEX"):
                continue
            slug = "file-" + re.sub(r"[^a-zA-Z0-9._-]+", "_", src.stem)
            body = md_to_html(src.read_text(encoding="utf-8"))
            (OUT / f"{slug}.html").write_text(page(src.stem, body), encoding="utf-8")
            entries.append((src.stem, slug))
        print(f"ok: {len(entries)} per-file pages")

    # per-file index page
    rows = "".join(f'<tr><td><a href="{slug}.html">{html.escape(name)}</a></td></tr>'
                   for name, slug in entries)
    idx_body = ("<h1>راهنمای فایل‌به‌فایل</h1>"
                "<p>برای هر فایل از کد، یک راهنمای کامل فارسی نوشته شده است:</p>"
                f"<table><tbody>{rows}</tbody></table>")
    (OUT / "files-index.html").write_text(page("فایل‌به‌فایل", idx_body), encoding="utf-8")

    # enrich the main index with links to per-file docs
    print(f"generated {len(generated)} guide pages + files index in {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
