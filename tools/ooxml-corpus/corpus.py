#!/usr/bin/env python3
"""Build a small OOXML corpus, one file per feature area, for the wasm-ui audit.

Each file is hand-written WordprocessingML so the feature under test is the only
thing in it. `.docx` packaging is minimal (content types + rels + parts).
"""
import os
import struct
import zipfile
import zlib

NS = ('xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
      'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
      'xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" '
      'xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
      'xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture" '
      'xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math"')

CT_HEAD = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
           '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
           '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
           '<Default Extension="xml" ContentType="application/xml"/>'
           '<Default Extension="png" ContentType="image/png"/>'
           '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
           '<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>')
CT_FOOTNOTES = ('<Override PartName="/word/footnotes.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml"/>')
CT_ENDNOTES = ('<Override PartName="/word/endnotes.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.endnotes+xml"/>')
CT_COMMENTS = ('<Override PartName="/word/comments.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.comments+xml"/>')
CT_HEADER = ('<Override PartName="/word/header1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml"/>')
CT_FOOTER = ('<Override PartName="/word/footer1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/>')
CT_NUMBERING = ('<Override PartName="/word/numbering.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"/>')

RELS_HEAD = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
             '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
             '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>'
             '</Relationships>')

STYLES = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
          f'<w:styles {NS}><w:docDefaults/><w:style w:type="paragraph" w:default="1" w:styleId="Normal">'
          '<w:name w:val="Normal"/><w:pPr><w:spacing w:after="120" w:line="276" w:lineRule="auto"/></w:pPr>'
          '<w:rPr><w:sz w:val="22"/></w:rPr></w:style>'
          '<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/>'
          '<w:basedOn w:val="Normal"/><w:pPr><w:outlineLvl w:val="0"/></w:pPr>'
          '<w:rPr><w:b/><w:sz w:val="36"/></w:rPr></w:style>'
          '<w:style w:type="paragraph" w:styleId="Quoted"><w:name w:val="Quote"/>'
          '<w:basedOn w:val="Normal"/><w:pPr><w:ind w:left="720"/><w:jc w:val="both"/></w:pPr></w:style>'
          '</w:styles>')


def run(text, rpr=''):
    return f'<w:r>{rpr}<w:t xml:space="preserve">{text}</w:t></w:r>'


def para(inner, ppr=''):
    return f'<w:p>{ppr}{inner}</w:p>'


def tiny_png():
    """1x1 grey PNG (zlib-compressed by hand, no PIL needed)."""
    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))
    raw = b'\x00\x80\x80\x80'
    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw)) +
            chunk(b'IEND', b''))


def doc(body, sect=''):
    if not sect:
        sect = ('<w:sectPr><w:pgSz w:w="11906" w:h="16838"/>'
                '<w:pgMar w:top="1418" w:right="1418" w:bottom="1418" w:left="1418" '
                'w:header="709" w:footer="709" w:gutter="0"/></w:sectPr>')
    return (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
            f'<w:document {NS}><w:body>{body}{sect}</w:body></w:document>')


def write(path, body, sect='', extra_parts=None, extra_rels='', ct_extra='',
          docrels_extra=''):
    extra_parts = extra_parts or {}
    ct = CT_HEAD + ct_extra + '</Types>'
    drels = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
             '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
             '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>'
             + docrels_extra + extra_rels + '</Relationships>')
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml', ct)
        z.writestr('_rels/.rels', RELS_HEAD)
        z.writestr('word/_rels/document.xml.rels', drels)
        z.writestr('word/document.xml', doc(body, sect))
        z.writestr('word/styles.xml', STYLES)
        for name, data in extra_parts.items():
            z.writestr(name, data)


# ── case 1: character formatting ───────────────────────────────────────────
def case_runs(out):
    body = para(
        run('plain ', '')
        + run('bold ', '<w:rPr><w:b/></w:rPr>')
        + run('italic ', '<w:rPr><w:i/></w:rPr>')
        + run('strike ', '<w:rPr><w:strike/></w:rPr>')
        + run('super', '<w:rPr><w:vertAlign w:val="superscript"/></w:rPr>')
        + run(' sub', '<w:rPr><w:vertAlign w:val="subscript"/></w:rPr>')
        + run(' red', '<w:rPr><w:color w:val="FF0000"/></w:rPr>')
        + run(' hilite', '<w:rPr><w:highlight w:val="yellow"/></w:rPr>')
        + run(' caps', '<w:rPr><w:caps/></w:rPr>')
        + run(' smallcaps', '<w:rPr><w:smallCaps/></w:rPr>')
        + run(' spaced', '<w:rPr><w:spacing w:val="40"/></w:rPr>')
        + run(' courier', '<w:rPr><w:rFonts w:ascii="Courier New"/><w:sz w:val="30"/></w:rPr>')
    )
    write(out, body)


# ── case 2: hyperlink, bookmark, field ─────────────────────────────────────
def case_fields(out):
    body = (para('<w:hyperlink r:id="rId9">' + run('HYPERLINK TEXT') + '</w:hyperlink>')
            + para(run('Page ') + '<w:fldSimple w:instr=" PAGE ">' + run('7') + '</w:fldSimple>')
            + para('<w:r><w:fldChar w:fldCharType="begin"/></w:r>'
                   '<w:r><w:instrText xml:space="preserve"> TOC \\o "1-3" </w:instrText></w:r>'
                   '<w:r><w:fldChar w:fldCharType="separate"/></w:r>'
                   + run('TOC ENTRY TEXT') +
                   '<w:r><w:fldChar w:fldCharType="end"/></w:r>')
            + para('<w:bookmarkStart w:id="0" w:name="MARK1"/>' + run('BOOKMARK BODY')
                   + '<w:bookmarkEnd w:id="0"/>'))
    write(out, body, docrels_extra='<Relationship Id="rId9" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com" TargetMode="External"/>')


# ── case 3: footnote + endnote ─────────────────────────────────────────────
def case_notes(out):
    fn = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:footnotes {NS}>'
          '<w:footnote w:id="1">' + para(run('FOOTNOTE MARKER')) + '</w:footnote></w:footnotes>')
    en = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:endnotes {NS}>'
          '<w:endnote w:id="1">' + para(run('ENDNOTE MARKER')) + '</w:endnote></w:endnotes>')
    body = para(run('Text with a note') +
                '<w:r><w:footnoteReference w:id="1"/></w:r>' +
                run(' and an endnote') +
                '<w:r><w:endnoteReference w:id="1"/></w:r>')
    write(out, body,
          extra_parts={'word/footnotes.xml': fn, 'word/endnotes.xml': en},
          ct_extra=CT_FOOTNOTES + CT_ENDNOTES,
          docrels_extra='<Relationship Id="rId20" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes" Target="footnotes.xml"/>'
                        '<Relationship Id="rId21" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/endnotes" Target="endnotes.xml"/>')


# ── case 4: comment + tracked change ───────────────────────────────────────
def case_review(out):
    cm = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:comments {NS}>'
          '<w:comment w:id="1" w:author="A">' + para(run('COMMENT MARKER')) + '</w:comment></w:comments>')
    body = (para(run('Reviewed text') +
                 '<w:r><w:commentReference w:id="1"/></w:r>')
            + para('<w:ins w:id="2" w:author="A">' + run('INSERTED TEXT') + '</w:ins>'
                   '<w:del w:id="3" w:author="A"><w:r><w:delText>DELETED TEXT</w:delText></w:r></w:del>'))
    write(out, body, extra_parts={'word/comments.xml': cm}, ct_extra=CT_COMMENTS,
          docrels_extra='<Relationship Id="rId30" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments" Target="comments.xml"/>')


# ── case 5: header / footer ────────────────────────────────────────────────
def case_header(out):
    hdr = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:hdr {NS}>'
           + para(run('HEADER MARKER')) + '</w:hdr>')
    ftr = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:ftr {NS}>'
           + para(run('FOOTER MARKER')) + '</w:ftr>')
    sect = ('<w:sectPr><w:headerReference w:type="default" r:id="rId40"/>'
            '<w:footerReference w:type="default" r:id="rId41"/>'
            '<w:pgSz w:w="11906" w:h="16838"/>'
            '<w:pgMar w:top="1418" w:right="1418" w:bottom="1418" w:left="1418" '
            'w:header="709" w:footer="709"/></w:sectPr>')
    write(out, para(run('Body text just here')), sect=sect,
          extra_parts={'word/header1.xml': hdr, 'word/footer1.xml': ftr},
          ct_extra=CT_HEADER + CT_FOOTER,
          docrels_extra='<Relationship Id="rId40" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/header" Target="header1.xml"/>'
                        '<Relationship Id="rId41" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer" Target="footer1.xml"/>')


# ── case 6: numbering (multilevel, decimal/bullet) ─────────────────────────
def case_numbering(out):
    numbering = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:numbering {NS}>'
                 '<w:abstractNum w:abstractNumId="0">'
                 '<w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="decimal"/>'
                 '<w:lvlText w:val="%1."/></w:lvl>'
                 '<w:lvl w:ilvl="1"><w:start w:val="1"/><w:numFmt w:val="lowerLetter"/>'
                 '<w:lvlText w:val="%2)"/></w:lvl>'
                 '</w:abstractNum><w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num>'
                 '</w:numbering>')
    def item(text, ilvl=0):
        return para(run(text), ppr=f'<w:pPr><w:numPr><w:ilvl w:val="{ilvl}"/>'
                                   f'<w:numId w:val="1"/></w:numPr></w:pPr>')
    body = item('NUMBER ITEM ONE') + item('NUMBER ITEM ONE A', 1) + item('NUMBER ITEM TWO')
    write(out, body, extra_parts={'word/numbering.xml': numbering},
          ct_extra=CT_NUMBERING,
          docrels_extra='<Relationship Id="rId50" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering" Target="numbering.xml"/>')


# ── case 7: paragraph formatting ───────────────────────────────────────────
def case_paragraphs(out):
    body = (para(run('CENTERED PARAGRAPH'),
                 '<w:pPr><w:jc w:val="center"/><w:spacing w:before="240" w:after="120"/></w:pPr>')
            + para(run('JUSTIFIED PARAGRAPH with enough words to wrap over a line or two so the '
                       'alignment and indents are visible in the export.'),
                   '<w:pPr><w:jc w:val="both"/><w:ind w:left="720" w:firstLine="360"/></w:pPr>')
            + para(run('HANGING INDENT LIST ITEM'),
                   '<w:pPr><w:ind w:left="720" w:hanging="360"/></w:pPr>')
            + para(run('SHADED BORDERED'),
                   '<w:pPr><w:shd w:val="clear" w:fill="DDEEFF"/>'
                   '<w:pBdr><w:top w:val="single" w:sz="8"/>'
                   '<w:bottom w:val="single" w:sz="8"/></w:pBdr></w:pPr>')
            + para(run('TAB\tWITH\tSTOPS'),
                   '<w:pPr><w:tabs><w:tab w:val="left" w:pos="1440" w:leader="dot"/>'
                   '<w:tab w:val="right" w:pos="8640"/></w:tabs></w:pPr>')
            + para(run('KEEP WITH NEXT'),
                   '<w:pPr><w:keepNext/><w:widowControl/></w:pPr>'))
    write(out, body)


# ── case 8: table with merge, shading, header repeat ───────────────────────
def case_table(out):
    def cell(text, props=''):
        return f'<w:tc><w:tcPr>{props}</w:tcPr>{para(run(text))}</w:tc>'
    tbl = ('<w:tbl><w:tblPr><w:tblW w:w="0" w:type="auto"/>'
           '<w:tblBorders><w:top w:val="single" w:sz="8"/><w:bottom w:val="single" w:sz="8"/>'
           '<w:insideH w:val="single" w:sz="4"/></w:tblBorders></w:tblPr>'
           '<w:tr><w:trPr><w:tblHeader/></w:trPr>'
           + cell('HEADER CELL', '<w:shd w:val="clear" w:fill="DDDDDD"/>')
           + cell('SECOND HEADER') + '</w:tr>'
           '<w:tr>' + cell('MERGED ACROSS', '<w:gridSpan w:val="2"/>') + '</w:tr>'
           '<w:tr>' + cell('VERT MERGE TOP', '<w:vMerge w:val="restart"/>')
           + cell('CELL B') + '</w:tr>'
           '<w:tr>' + cell('', '<w:vMerge/>') + cell('CELL D') + '</w:tr>'
           '</w:tbl>')
    body = tbl + para(run('TABLE CAPTION HERE'))
    write(out, body)


# ── case 9: section properties (landscape + margins + 2 cols) ──────────────
def case_section(out):
    sect = ('<w:sectPr><w:pgSz w:w="16838" w:h="11906" w:orient="landscape"/>'
            '<w:pgMar w:top="1134" w:right="1701" w:bottom="1134" w:left="1701" '
            'w:header="709" w:footer="709"/><w:cols w:num="2" w:space="425"/>'
            '<w:titlePg/><w:pgNumType w:start="3"/><w:lnNumType w:countBy="1"/>'
            '</w:sectPr>')
    body = ''.join(para(run(f'SECTION PROBE PARAGRAPH {i} with text.')) for i in range(1, 8))
    write(out, body, sect=sect)


# ── case 10: inline image ──────────────────────────────────────────────────
def case_image(out):
    rid = 'rId60'
    drawing = ('<w:r><w:drawing><wp:inline><wp:extent cx="635000" cy="635000"/>'
               '<a:graphic><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture">'
               '<pic:pic><pic:nvPicPr><pic:cNvPr id="1" name="p"/><pic:cNvPicPr/></pic:nvPicPr>'
               f'<pic:blipFill><a:blip r:embed="{rid}"/></pic:blipFill>'
               '<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="635000" cy="635000"/></a:xfrm>'
               '<a:prstGeom prst="rect"/></pic:spPr></pic:pic>'
               '</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>')
    body = para(run('Image follows') + drawing)
    write(out, body,
          extra_parts={'word/media/image1.png': tiny_png()},
          docrels_extra=f'<Relationship Id="{rid}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="media/image1.png"/>')


CASES = {
    'runs': case_runs,
    'fields': case_fields,
    'notes': case_notes,
    'review': case_review,
    'header': case_header,
    'numbering': case_numbering,
    'paragraphs': case_paragraphs,
    'table': case_table,
    'section': case_section,
    'image': case_image,
}

if __name__ == '__main__':
    outdir = os.environ.get('OUTDIR', '/home/hermes/.hermes/cache/scratch/ooxml')
    os.makedirs(outdir, exist_ok=True)
    for name, fn in CASES.items():
        path = os.path.join(outdir, f'{name}.docx')
        fn(path)
        print(f'{path}  {os.path.getsize(path)} bytes')
