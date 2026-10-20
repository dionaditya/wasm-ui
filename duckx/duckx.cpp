#include "duckx.hpp"

#include <cctype>

// Hack on pugixml
// We need to write xml to std string (or char *)
// So overload the write function
struct xml_string_writer : pugi::xml_writer {
    std::string result;

    virtual void write(const void *data, size_t size) {
        result.append(static_cast<const char *>(data), size);
    }
};

duckx::Run::Run() {}

duckx::Run::Run(pugi::xml_node parent, pugi::xml_node current) {
    this->set_parent(parent);
    this->set_current(current);
}

void duckx::Run::set_parent(pugi::xml_node node) {
    this->parent = node;
    this->current = this->parent.child("w:r");
}

void duckx::Run::set_current(pugi::xml_node node) { this->current = node; }

std::string duckx::Run::get_text() const {
    return this->current.child("w:t").text().get();
}

bool duckx::Run::set_text(const std::string &text) const {
    return this->current.child("w:t").text().set(text.c_str());
}

bool duckx::Run::set_text(const char *text) const {
    return this->current.child("w:t").text().set(text);
}

duckx::Run &duckx::Run::next() {
    this->current = this->current.next_sibling();
    return *this;
}

bool duckx::Run::has_next() const { return this->current != 0; }

// Table cells
duckx::TableCell::TableCell() {}

duckx::TableCell::TableCell(pugi::xml_node parent, pugi::xml_node current) {
    this->set_parent(parent);
    this->set_current(current);
}

void duckx::TableCell::set_parent(pugi::xml_node node) {
    this->parent = node;
    this->current = this->parent.child("w:tc");

    this->paragraph.set_parent(this->current);
}

void duckx::TableCell::set_current(pugi::xml_node node) {
    this->current = node;
}

bool duckx::TableCell::has_next() const { return this->current != 0; }

duckx::TableCell &duckx::TableCell::next() {
    this->current = this->current.next_sibling();
    return *this;
}

duckx::Paragraph &duckx::TableCell::paragraphs() {
    this->paragraph.set_parent(this->current);
    return this->paragraph;
}

// Table rows
duckx::TableRow::TableRow() {}

duckx::TableRow::TableRow(pugi::xml_node parent, pugi::xml_node current) {
    this->set_parent(parent);
    this->set_current(current);
}

void duckx::TableRow::set_parent(pugi::xml_node node) {
    this->parent = node;
    this->current = this->parent.child("w:tr");

    this->cell.set_parent(this->current);
}

void duckx::TableRow::set_current(pugi::xml_node node) { this->current = node; }

duckx::TableRow &duckx::TableRow::next() {
    this->current = this->current.next_sibling();
    return *this;
}

duckx::TableCell &duckx::TableRow::cells() {
    this->cell.set_parent(this->current);
    return this->cell;
}

bool duckx::TableRow::has_next() const { return this->current != 0; }

// Tables
duckx::Table::Table() {}

duckx::Table::Table(pugi::xml_node parent, pugi::xml_node current) {
    this->set_parent(parent);
    this->set_current(current);
}

void duckx::Table::set_parent(pugi::xml_node node) {
    this->parent = node;
    this->current = this->parent.child("w:tbl");

    this->row.set_parent(this->current);
}

bool duckx::Table::has_next() const { return this->current != 0; }

duckx::Table &duckx::Table::next() {
    this->current = this->current.next_sibling();
    this->row.set_parent(this->current);
    return *this;
}

void duckx::Table::set_current(pugi::xml_node node) { this->current = node; }

duckx::TableRow &duckx::Table::rows() {
    this->row.set_parent(this->current);
    return this->row;
}

duckx::Paragraph::Paragraph() {}

duckx::Paragraph::Paragraph(pugi::xml_node parent, pugi::xml_node current) {
    this->set_parent(parent);
    this->set_current(current);
}

void duckx::Paragraph::set_parent(pugi::xml_node node) {
    this->parent = node;
    this->current = this->parent.child("w:p");

    this->run.set_parent(this->current);
}

void duckx::Paragraph::set_current(pugi::xml_node node) {
    this->current = node;
}

duckx::Paragraph &duckx::Paragraph::next() {
    this->current = this->current.next_sibling("w:p");
    this->run.set_parent(this->current);
    return *this;
}

bool duckx::Paragraph::has_next() const {
    return this->current && std::string(this->current.name()) == "w:p";
}

bool duckx::Paragraph::has_next_sibling() const {
    return this->current.next_sibling("w:p") != pugi::xml_node();
}

void duckx::Paragraph::clear_runs() {
    pugi::xml_node n = this->current.child("w:r");
    while (n) {
        pugi::xml_node next = n.next_sibling("w:r");
        this->current.remove_child(n);
        n = next;
    }
}

duckx::Run &duckx::Paragraph::runs() {
    this->run.set_parent(this->current);
    return this->run;
}

duckx::Run &duckx::Paragraph::add_run_ex(const char *text, duckx::formatting_flag f,
                                         const char *colorHex, const char *highlightName,
                                         const char *fontName, float sizePt) {
    pugi::xml_node new_run = this->current.append_child("w:r");
    pugi::xml_node meta = new_run.append_child("w:rPr");

    // Order matters for strict OOXML consumers: rFonts, b, i, color, sz,
    // highlight, u, strike, vertAlign.
    if (fontName && *fontName) {
        pugi::xml_node rf = meta.append_child("w:rFonts");
        rf.append_attribute("w:ascii").set_value(fontName);
        rf.append_attribute("w:hAnsi").set_value(fontName);
        rf.append_attribute("w:cs").set_value(fontName);
    }
    if (f & duckx::bold)   meta.append_child("w:b");
    if (f & duckx::italic) meta.append_child("w:i");
    if (colorHex && *colorHex)
        meta.append_child("w:color").append_attribute("w:val").set_value(colorHex);
    if (sizePt > 0.f) {
        char half[16];
        snprintf(half, sizeof(half), "%d", (int)(sizePt * 2.f + 0.5f));
        meta.append_child("w:sz").append_attribute("w:val").set_value(half);
        meta.append_child("w:szCs").append_attribute("w:val").set_value(half);
    }
    if (highlightName && *highlightName)
        meta.append_child("w:highlight").append_attribute("w:val").set_value(highlightName);
    if (f & duckx::underline)
        meta.append_child("w:u").append_attribute("w:val").set_value("single");
    if (f & duckx::strikethrough)
        meta.append_child("w:strike").append_attribute("w:val").set_value("true");
    if (f & duckx::superscript)
        meta.append_child("w:vertAlign").append_attribute("w:val").set_value("superscript");
    else if (f & duckx::subscript)
        meta.append_child("w:vertAlign").append_attribute("w:val").set_value("subscript");
    if (f & duckx::smallcaps)
        meta.append_child("w:smallCaps").append_attribute("w:val").set_value("true");

    pugi::xml_node t = new_run.append_child("w:t");
    if (text && *text && (isspace((unsigned char)text[0]) ||
                          isspace((unsigned char)text[strlen(text) - 1])))
        t.append_attribute("xml:space").set_value("preserve");
    t.text().set(text ? text : "");

    return *new Run(this->current, new_run);
}

void duckx::Paragraph::set_para_props(const char *jc, int indLeftTwips,
                                      int spaceBeforeTwips, int spaceAfterTwips,
                                      const char *shdFill, int hangingTwips) {
    // drop any existing w:pPr (keep it simple — the export owns the layout)
    for (pugi::xml_node ch = this->current.first_child(); ch;) {
        pugi::xml_node nxt = ch.next_sibling();
        if (std::string(ch.name()) == "w:pPr") this->current.remove_child(ch);
        ch = nxt;
    }
    pugi::xml_node ppr = this->current.prepend_child("w:pPr");
    if (shdFill && *shdFill)
        ppr.append_child("w:shd").append_attribute("w:fill").set_value(shdFill);
    if (spaceBeforeTwips >= 0 || spaceAfterTwips >= 0) {
        pugi::xml_node sp = ppr.append_child("w:spacing");
        if (spaceBeforeTwips >= 0) sp.append_attribute("w:before").set_value(spaceBeforeTwips);
        if (spaceAfterTwips >= 0)  sp.append_attribute("w:after").set_value(spaceAfterTwips);
    }
    if (indLeftTwips > 0 || hangingTwips > 0) {
        pugi::xml_node ind = ppr.append_child("w:ind");
        if (indLeftTwips > 0) ind.append_attribute("w:left").set_value(indLeftTwips);
        if (hangingTwips > 0) ind.append_attribute("w:hanging").set_value(hangingTwips);
    }
    if (jc && *jc)
        ppr.append_child("w:jc").append_attribute("w:val").set_value(jc);
}

void duckx::Paragraph::insert_table_after(int rows, int cols,
                                          const std::vector<std::string> &cells,
                                          int widthTwips, float borderPt,
                                          const char *borderHex, const char *headerFillHex) {
    pugi::xml_node body = this->current.parent();
    if (!body) return;

    pugi::xml_node tbl = body.insert_child_after("w:tbl", this->current);
    pugi::xml_node pr = tbl.append_child("w:tblPr");
    pugi::xml_node tw = pr.append_child("w:tblW");
    tw.append_attribute("w:w").set_value(widthTwips);
    tw.append_attribute("w:type").set_value("dxa");
    pugi::xml_node borders = pr.append_child("w:tblBorders");
    const char *sides[6] = {"top", "left", "bottom", "right", "insideH", "insideV"};
    int bSz = (int)(borderPt * 8.f + 0.5f);       // eighths of a point
    if (bSz > 96) bSz = 96;
    for (int i = 0; i < 6; ++i) {
        std::string nm = std::string("w:") + sides[i];
        pugi::xml_node b = borders.append_child(nm.c_str());
        b.append_attribute("w:val").set_value(bSz > 0 ? "single" : "none");
        b.append_attribute("w:sz").set_value(bSz > 0 ? bSz : 0);
        b.append_attribute("w:space").set_value(0);
        b.append_attribute("w:color").set_value(borderHex ? borderHex : "B4BCD0");
    }

    const int colW = (cols > 0) ? widthTwips / cols : widthTwips;
    for (int r = 0; r < rows; ++r) {
        pugi::xml_node tr = tbl.append_child("w:tr");
        for (int c = 0; c < cols; ++c) {
            pugi::xml_node tc = tr.append_child("w:tc");
            pugi::xml_node tcPr = tc.append_child("w:tcPr");
            pugi::xml_node cw = tcPr.append_child("w:tcW");
            cw.append_attribute("w:w").set_value(colW);
            cw.append_attribute("w:type").set_value("dxa");
            if (r == 0 && headerFillHex && *headerFillHex) {
                pugi::xml_node shd = tcPr.append_child("w:shd");
                shd.append_attribute("w:val").set_value("clear");
                shd.append_attribute("w:color").set_value("auto");
                shd.append_attribute("w:fill").set_value(headerFillHex);
            }
            pugi::xml_node para = tc.append_child("w:p");
            pugi::xml_node run  = para.append_child("w:r");
            size_t idx = (size_t)r * cols + c;
            const char *txt = (idx < cells.size()) ? cells[idx].c_str() : "";
            pugi::xml_node t = run.append_child("w:t");
            if (txt && *txt && (isspace((unsigned char)txt[0]) ||
                                isspace((unsigned char)txt[strlen(txt) - 1])))
                t.append_attribute("xml:space").set_value("preserve");
            t.text().set(txt ? txt : "");
        }
    }

    // trailing paragraph after the table, cursor moves there
    pugi::xml_node follow = body.insert_child_after("w:p", tbl);
    this->current = follow;
    this->run.set_parent(follow);
}

void duckx::Paragraph::set_para_border_bottom(float pt, const char *colorHex) {
    pugi::xml_node ppr = this->current.child("w:pPr");
    if (!ppr) ppr = this->current.prepend_child("w:pPr");
    pugi::xml_node bdr = ppr.prepend_child("w:pBdr");
    pugi::xml_node b = bdr.append_child("w:bottom");
    int sz = (int)(pt * 8.f + 0.5f);
    if (sz < 1) sz = 1;
    b.append_attribute("w:val").set_value("single");
    b.append_attribute("w:sz").set_value(sz);
    b.append_attribute("w:space").set_value(1);
    b.append_attribute("w:color").set_value(colorHex ? colorHex : "8A90A0");
}

void duckx::Paragraph::remove_self() {
    if (this->current) {
        pugi::xml_node par = this->current.parent();
        if (par) par.remove_child(this->current);
        this->current = pugi::xml_node();
    }
}

duckx::Run &duckx::Paragraph::add_run(const std::string &text,
                                      duckx::formatting_flag f) {
    return this->add_run(text.c_str(), f);
}

duckx::Run &duckx::Paragraph::add_run(const char *text,
                                      duckx::formatting_flag f) {
    // Add new run
    pugi::xml_node new_run = this->current.append_child("w:r");
    // Insert meta to new run
    pugi::xml_node meta = new_run.append_child("w:rPr");

    if (f & duckx::bold)
        meta.append_child("w:b");

    if (f & duckx::italic)
        meta.append_child("w:i");

    if (f & duckx::underline)
        meta.append_child("w:u").append_attribute("w:val").set_value("single");

    if (f & duckx::strikethrough)
        meta.append_child("w:strike")
            .append_attribute("w:val")
            .set_value("true");

    if (f & duckx::superscript)
        meta.append_child("w:vertAlign")
            .append_attribute("w:val")
            .set_value("superscript");
    else if (f & duckx::subscript)
        meta.append_child("w:vertAlign")
            .append_attribute("w:val")
            .set_value("subscript");

    if (f & duckx::smallcaps)
        meta.append_child("w:smallCaps")
            .append_attribute("w:val")
            .set_value("true");

    if (f & duckx::shadow)
        meta.append_child("w:shadow")
            .append_attribute("w:val")
            .set_value("true");

    pugi::xml_node new_run_text = new_run.append_child("w:t");
    // If the run starts or ends with whitespace characters, preserve them using
    // the xml:space attribute
    if (*text != 0 && (isspace(text[0]) || isspace(text[strlen(text) - 1])))
        new_run_text.append_attribute("xml:space").set_value("preserve");
    new_run_text.text().set(text);

    return *new Run(this->current, new_run);
}

void duckx::Paragraph::inject_drawing(int imgSeq, int widthEmu, int heightEmu) {
    char rId[32]; snprintf(rId, 32, "rId_img%d", imgSeq);
    char imgName[64]; snprintf(imgName, 64, "image%d.bmp", imgSeq);
    char cxBuf[24]; snprintf(cxBuf, 24, "%d", widthEmu);
    char cyBuf[24]; snprintf(cyBuf, 24, "%d", heightEmu);
    char idBuf[16]; snprintf(idBuf, 16, "%d", imgSeq+1);

    // Build the drawing XML as a parseable fragment with all needed ns declarations inline
    std::string xml =
        std::string("<root xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""
        " xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\""
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">")
        + "<w:r xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
        + " xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        + "<w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
        + "<wp:extent cx=\"" + cxBuf + "\" cy=\"" + cyBuf + "\"/>"
        + "<wp:docPr id=\"" + idBuf + "\" name=\"" + imgName + "\"/>"
        + "<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
        + "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        + "<pic:pic><pic:nvPicPr>"
        + "<pic:cNvPr id=\"0\" name=\"" + imgName + "\"/><pic:cNvPicPr/>"
        + "</pic:nvPicPr><pic:blipFill>"
        + "<a:blip r:embed=\"" + rId + "\"/>"
        + "<a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        + "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
        + "<a:ext cx=\"" + cxBuf + "\" cy=\"" + cyBuf + "\"/></a:xfrm>"
        + "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
        + "</pic:spPr></pic:pic></a:graphicData></a:graphic>"
        + "</wp:inline></w:drawing></w:r></root>";

    pugi::xml_document tmp;
    tmp.load_string(xml.c_str());
    pugi::xml_node wrun = tmp.child("root").child("w:r");
    this->current.append_copy(wrun);
}

void duckx::Paragraph::inject_drawing_anchor(int imgSeq, int widthEmu, int heightEmu, bool behindDoc) {
    char rId[32]; snprintf(rId, 32, "rId_img%d", imgSeq);
    char imgName[64]; snprintf(imgName, 64, "image%d.bmp", imgSeq);
    char cxBuf[24]; snprintf(cxBuf, 24, "%d", widthEmu);
    char cyBuf[24]; snprintf(cyBuf, 24, "%d", heightEmu);
    char idBuf[16]; snprintf(idBuf, 16, "%d", imgSeq+1);
    const char* bd = behindDoc ? "1" : "0";

    std::string xml =
        std::string("<root xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""
        " xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\""
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">")
        + "<w:r xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
        + " xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        + "<w:drawing><wp:anchor behindDoc=\"" + bd + "\""
        + " distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\""
        + " simplePos=\"0\" relativeHeight=\"251658240\" allowOverlap=\"1\">"
        + "<wp:simplePos x=\"0\" y=\"0\"/>"
        + "<wp:positionH relativeFrom=\"column\"><wp:posOffset>0</wp:posOffset></wp:positionH>"
        + "<wp:positionV relativeFrom=\"paragraph\"><wp:posOffset>0</wp:posOffset></wp:positionV>"
        + "<wp:extent cx=\"" + cxBuf + "\" cy=\"" + cyBuf + "\"/>"
        + "<wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>"
        + "<wp:wrapNone/>"
        + "<wp:docPr id=\"" + idBuf + "\" name=\"" + imgName + "\"/>"
        + "<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
        + "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        + "<pic:pic><pic:nvPicPr>"
        + "<pic:cNvPr id=\"0\" name=\"" + imgName + "\"/><pic:cNvPicPr/>"
        + "</pic:nvPicPr><pic:blipFill>"
        + "<a:blip r:embed=\"" + rId + "\"/>"
        + "<a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        + "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
        + "<a:ext cx=\"" + cxBuf + "\" cy=\"" + cyBuf + "\"/></a:xfrm>"
        + "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
        + "</pic:spPr></pic:pic></a:graphicData></a:graphic>"
        + "</wp:anchor></w:drawing></w:r></root>";

    pugi::xml_document tmp;
    tmp.load_string(xml.c_str());
    pugi::xml_node wrun = tmp.child("root").child("w:r");
    this->current.append_copy(wrun);
}

duckx::Paragraph &
duckx::Paragraph::insert_paragraph_after(const std::string &text,
                                         duckx::formatting_flag f) {
    pugi::xml_node new_para =
        this->parent.insert_child_after("w:p", this->current);

    Paragraph *p = new Paragraph();
    p->set_current(new_para);
    p->add_run(text, f);

    return *p;
}

duckx::Document::Document() {
    // TODO: this function must be removed!
    this->directory = "";
}

duckx::Document::Document(std::string directory) {
    this->directory = directory;
}

void duckx::Document::file(std::string directory) {
    this->directory = directory;
}

void duckx::Document::open() {
    void *buf = NULL;
    size_t bufsize;

    // Open file and load "xml" content to the document variable
    zip_t *zip =
        zip_open(this->directory.c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'r');

    if (!zip) {
        this->flag_is_open = false;
        return;
    }
    this->flag_is_open = true;

    zip_entry_open(zip, "word/document.xml");
    zip_entry_read(zip, &buf, &bufsize);

    zip_entry_close(zip);
    zip_close(zip);

    this->document.load_buffer(buf, bufsize);

    free(buf);

    this->paragraph.set_parent(document.child("w:document").child("w:body"));
}

void duckx::Document::save() const {
    // minizip only supports appending or writing to new files
    // so we must
    // - make a new file
    // - write any new files
    // - copy the old files
    // - delete old docx
    // - rename new file to old file
    
    if(!this->is_open()) {
        // if file is not existing, save() will make no sense
        return;
    }

    // Read document buffer
    xml_string_writer writer;
    this->document.print(writer);

    // Open file and replace "xml" content

    std::string original_file = this->directory;
    std::string temp_file = this->directory + ".tmp";

    // Create the new file
    zip_t *new_zip =
        zip_open(temp_file.c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

    // Write out document.xml
    zip_entry_open(new_zip, "word/document.xml");

    const char *buf = writer.result.c_str();

    zip_entry_write(new_zip, buf, strlen(buf));
    zip_entry_close(new_zip);

    // Open the original zip and copy all files which are not replaced by duckX
    zip_t *orig_zip =
        zip_open(original_file.c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'r');

    // Loop & copy each relevant entry in the original zip
    int orig_zip_entry_ct = zip_total_entries(orig_zip);
    for (int i = 0; i < orig_zip_entry_ct; i++) {
        zip_entry_openbyindex(orig_zip, i);
        const char *name = zip_entry_name(orig_zip);

        // Skip copying the original file
        if (std::string(name) != std::string("word/document.xml")) {
            // Read the old content
            void *entry_buf;
            size_t entry_buf_size;
            zip_entry_read(orig_zip, &entry_buf, &entry_buf_size);

            // Write into new zip
            zip_entry_open(new_zip, name);
            zip_entry_write(new_zip, entry_buf, entry_buf_size);
            zip_entry_close(new_zip);

            free(entry_buf);
        }

        zip_entry_close(orig_zip);
    }

    // Close both zips
    zip_close(orig_zip);
    zip_close(new_zip);

    // Remove original zip, rename new to correct name
    remove(original_file.c_str());
    rename(temp_file.c_str(), original_file.c_str());
}

bool duckx::Document::is_open() const {
    return this->flag_is_open;
}

duckx::Paragraph &duckx::Document::paragraphs() {
    this->paragraph.set_parent(document.child("w:document").child("w:body"));
    return this->paragraph;
}

duckx::Table &duckx::Document::tables() {
    this->table.set_parent(document.child("w:document").child("w:body"));
    return this->table;
}
