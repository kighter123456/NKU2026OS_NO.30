"""Mirror the linked HonKit book and its static dependencies."""

import json
import posixpath
import threading
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from urllib.parse import quote, unquote, urljoin, urlsplit, urlunsplit

import requests
import tinycss2
from bs4 import BeautifulSoup
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry


BASE = "http://8.135.34.58/lab2026/_book/"
ROOT = Path(__file__).resolve().parents[1]
ORIGIN = urlsplit(BASE)
STATE = threading.local()


def session():
    if not hasattr(STATE, "session"):
        STATE.session = requests.Session()
        retry = Retry(total=3, backoff_factor=0.5, status_forcelist=[429, 500, 502, 503, 504])
        STATE.session.mount("http://", HTTPAdapter(max_retries=retry))
        STATE.session.headers["User-Agent"] = "HonKitOfflineMirror/1.0"
    return STATE.session


def internal_url(reference, parent):
    if not reference or reference.startswith(("#", "data:", "javascript:", "mailto:")):
        return None
    parts = urlsplit(urljoin(parent, reference))
    if parts.netloc != ORIGIN.netloc or not parts.path.startswith(ORIGIN.path):
        return None
    return urlunsplit((parts.scheme, parts.netloc, parts.path, "", ""))


def relative_path(url):
    path = unquote(urlsplit(url).path[len(ORIGIN.path):])
    if not path or path.endswith("/"):
        path += "index.html"
    parts = PurePosixPath(path)
    if parts.is_absolute() or ".." in parts.parts or "\\" in path or ":" in path:
        raise ValueError("URL escapes destination: " + url)
    return parts.as_posix()


def local_reference(reference, parent):
    target = internal_url(reference, parent)
    if not target:
        return reference
    path = posixpath.relpath(relative_path(target), posixpath.dirname(relative_path(parent)) or ".")
    parts = urlsplit(urljoin(parent, reference))
    return quote(path, safe="/@") + ("?" + parts.query if parts.query else "") + ("#" + parts.fragment if parts.fragment else "")


def css_references(text):
    references = []

    def walk(nodes):
        for node in nodes:
            if node.type == "url":
                references.append(node.value)
            elif node.type == "function" and node.lower_name == "url":
                args = [arg for arg in node.arguments if arg.type not in ("whitespace", "comment")]
                if args and args[0].type == "string":
                    references.append(args[0].value)
            elif node.type == "at-rule" and node.lower_at_keyword == "import":
                args = [arg for arg in node.prelude if arg.type not in ("whitespace", "comment")]
                if args and args[0].type == "string":
                    references.append(args[0].value)
            for attribute in ("content", "prelude", "arguments"):
                children = getattr(node, attribute, None)
                if children:
                    walk(children)

    walk(tinycss2.parse_stylesheet(text, skip_comments=True, skip_whitespace=True))
    return references


def fetch(url):
    try:
        response = session().get(url, timeout=(10, 45))
        response.raise_for_status()
        content = response.content
        content_type = response.headers.get("Content-Type", "")
        references = set()
        chapters = []
        is_html = "text/html" in content_type
        if is_html:
            soup = BeautifulSoup(content.decode("utf-8"), "html.parser")
            for link in soup.select(".summary a[href]"):
                target = internal_url(link["href"], url)
                if target:
                    chapters.append({"title": link.get_text(" ", strip=True), "path": relative_path(target)})
            for tag in soup.find_all(True):
                for attribute in ("href", "src", "poster", "data-src"):
                    value = tag.get(attribute)
                    if value:
                        target = internal_url(value, url)
                        if target:
                            references.add(target)
                            tag[attribute] = local_reference(value, url)
                if tag.get("srcset"):
                    entries = []
                    for entry in tag["srcset"].split(","):
                        fields = entry.strip().split()
                        if fields:
                            target = internal_url(fields[0], url)
                            if target:
                                references.add(target)
                                fields[0] = local_reference(fields[0], url)
                            entries.append(" ".join(fields))
                    tag["srcset"] = ", ".join(entries)
                style = tag.get("style")
                if style:
                    references.update(filter(None, (internal_url(ref, url) for ref in css_references("x {" + style + "}"))))
            for style in soup.find_all("style"):
                references.update(filter(None, (internal_url(ref, url) for ref in css_references(style.get_text()))))
            content = str(soup).encode("utf-8")
        elif urlsplit(url).path.endswith(".css"):
            references.update(filter(None, (internal_url(ref, url) for ref in css_references(content.decode("utf-8")))))
        target = ROOT / relative_path(url)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
        return {"url": url, "path": relative_path(url), "bytes": len(content), "html": is_html}, references, chapters
    except Exception as error:
        return {"url": url, "error": str(error)}, set(), []


def prepare_offline(files):
    # HonKit's AJAX navigation and JSON requests are blocked on file:// URLs.
    theme_path = ROOT / "gitbook/theme.js"
    original = "f=void 0!==history.pushState"
    replacement = 'f="file:"!==location.protocol&&void 0!==history.pushState'
    theme = theme_path.read_text(encoding="utf-8")
    if original not in theme and replacement not in theme:
        raise ValueError("Unrecognized HonKit navigation script")
    theme_path.write_text(theme.replace(original, replacement), encoding="utf-8")

    search_path = ROOT / "gitbook/gitbook-plugin-lunr/search-lunr.js"
    search = search_path.read_text(encoding="utf-8")
    if not search.startswith("var offlineSearchIndex = "):
        data = json.loads((ROOT / "search_index.json").read_text(encoding="utf-8"))
        for doc in data["store"].values():
            if doc["url"] in ("./", "", "/"):
                doc["url"] = "index.html"
        original = "$.getJSON(gitbook.state.basePath+'/search_index.json')"
        if original not in search:
            raise ValueError("Unrecognized HonKit search script")
        search = search.replace(original, "$.Deferred().resolve(offlineSearchIndex).promise()")
        search = "var offlineSearchIndex = " + json.dumps(data, ensure_ascii=True, separators=(",", ":")) + ";\n" + search
        search_path.write_text(search, encoding="utf-8")
    for record in files:
        record["bytes"] = (ROOT / record["path"]).stat().st_size


def main():
    pending = {BASE, urljoin(BASE, "search_index.json")}
    visited = set()
    files = []
    failures = []
    chapters = []
    manifest_path = ROOT / "download-manifest.json"
    if manifest_path.exists():
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        files = [item for item in previous["files"] if (ROOT / item["path"]).exists()]
        visited = {item["url"] for item in files}
        chapters = previous["chapters"]
        pending.update(item["url"] for item in previous["failures"])
    with ThreadPoolExecutor(max_workers=4) as pool:
        while pending:
            batch = sorted(pending - visited)
            pending.clear()
            visited.update(batch)
            for record, references, found_chapters in pool.map(fetch, batch):
                if "error" in record:
                    failures.append(record)
                    print("FAILED " + record["url"] + ": " + record["error"], flush=True)
                else:
                    files.append(record)
                    if record["url"] == BASE:
                        chapters = found_chapters
                    pending.update(references - visited)
            print("Downloaded: %d; queued: %d; failed: %d" % (len(files), len(pending), len(failures)), flush=True)
    if not failures:
        prepare_offline(files)
    manifest = {"source": BASE, "downloaded_at": datetime.now(timezone.utc).isoformat(), "offline_adjustments": ["Local navigation uses regular page loads on file URLs", "Search index embedded in the local Lunr search plugin"], "chapters": chapters, "files": sorted(files, key=lambda item: item["path"]), "failures": failures}
    (ROOT / "download-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print("HTML pages: %d; total files: %d; bytes: %d; failures: %d" % (sum(item["html"] for item in files), len(files), sum(item["bytes"] for item in files), len(failures)), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
