"""Package this declarative extension locally, using only Python's standard library."""

import json
from pathlib import Path
from xml.sax.saxutils import escape
from zipfile import ZIP_DEFLATED, ZipFile


def main():
    root = Path(__file__).resolve().parent
    metadata = json.loads((root / "package.json").read_text())
    output = root.parents[1] / "build" / f"{metadata['name']}-{metadata['version']}.vsix"
    output.parent.mkdir(exist_ok=True)

    # A VSIX is a ZIP containing the extension plus the editor's package metadata.
    # No extension-host code, npm dependencies, or marketplace publishing is needed.
    manifest = f'''<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="{escape(metadata['name'])}" Version="{escape(metadata['version'])}" Publisher="{escape(metadata['publisher'])}"/>
    <DisplayName>{escape(metadata['displayName'])}</DisplayName>
    <Description xml:space="preserve">{escape(metadata['description'])}</Description>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="{escape(metadata['engines']['vscode'])}"/>
    </Properties>
  </Metadata>
  <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code"/></Installation>
  <Dependencies/>
  <Assets><Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true"/></Assets>
</PackageManifest>
'''
    content_types = '''<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json"/>
  <Default Extension="md" ContentType="text/markdown"/>
  <Default Extension="vsixmanifest" ContentType="text/xml"/>
</Types>
'''
    with ZipFile(output, "w", ZIP_DEFLATED) as package:
        package.writestr("extension.vsixmanifest", manifest)
        package.writestr("[Content_Types].xml", content_types)
        for path in sorted(root.rglob("*.json")):
            json.loads(path.read_text())  # Fail packaging on malformed contributions.
            package.write(path, "extension/" + path.relative_to(root).as_posix())
        package.write(root / "README.md", "extension/README.md")
    print(output)


if __name__ == "__main__":
    main()
