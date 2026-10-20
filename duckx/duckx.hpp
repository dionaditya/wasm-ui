/*
 * Under MIT license
 * Author: Amir Mohamadi (@amiremohamadi)
 * DuckX is a free library to work with docx files.
 */

#ifndef DUCKX_H
#define DUCKX_H

#include <cstdio>
#include <stdlib.h>
#include <string>
#include <vector>

#include <constants.hpp>
#include <duckxiterator.hpp>
#include <pugixml.hpp>
#include <zip.h>

// TODO: Use container-iterator design pattern!

namespace duckx {
// Run contains runs in a paragraph
class Run {
  private:
    friend class IteratorHelper;
    // Store the parent node (a paragraph)
    pugi::xml_node parent;
    // And store current node also
    pugi::xml_node current;

  public:
    Run();
    Run(pugi::xml_node, pugi::xml_node);
    void set_parent(pugi::xml_node);
    void set_current(pugi::xml_node);

    std::string get_text() const;
    bool set_text(const std::string &) const;
    bool set_text(const char *) const;

    Run &next();
    bool has_next() const;
};

// Paragraph contains a paragraph
// and stores runs
class Paragraph {
  private:
    friend class IteratorHelper;
    // Store parent node (usually the body node)
    pugi::xml_node parent;
    // And store current node also
    pugi::xml_node current;
    // A paragraph consists of runs
    Run run;

  public:
    Paragraph();
    Paragraph(pugi::xml_node, pugi::xml_node);
    void set_parent(pugi::xml_node);
    void set_current(pugi::xml_node);

    Paragraph &next();
    bool has_next() const;
    bool has_next_sibling() const;
    void clear_runs();

    Run &runs();
    Run &add_run(const std::string &, duckx::formatting_flag = duckx::none);
    Run &add_run(const char *, duckx::formatting_flag = duckx::none);

    // Rich run: same as add_run plus font colour (RRGGBB), Word highlight name
    // ("yellow", "cyan", ...), font family and size in points.
    Run &add_run_ex(const char *text, duckx::formatting_flag f = duckx::none,
                    const char *colorHex = nullptr, const char *highlightName = nullptr,
                    const char *fontName = nullptr, float sizePt = 0.f);

    // Paragraph properties: alignment ("left"/"center"/"right"), left indent,
    // space before/after (twentieths of a point), shading fill (RRGGBB or null).
    void set_para_props(const char *jc, int indLeftTwips,
                        int spaceBeforeTwips, int spaceAfterTwips,
                        const char *shdFill, int hangingTwips = 0);

    // Remove this paragraph node from the document (used to trim template text).
    void remove_self();

    // Bottom border (Word's "horizontal rule": an empty paragraph with a rule).
    void set_para_border_bottom(float pt, const char *colorHex = "8A90A0");

    // Insert a w:tbl after this paragraph (plus a trailing empty paragraph) and
    // move the cursor onto that trailing paragraph so further writes stay in
    // document order.
    void insert_table_after(int rows, int cols, const std::vector<std::string> &cells,
                            int widthTwips, float borderPt = 1.f,
                            const char *borderHex = "B4BCD0",
                            const char *headerFillHex = nullptr);
    Paragraph &insert_paragraph_after(const std::string &,
                                      duckx::formatting_flag = duckx::none);
    void inject_drawing(int imgSeq, int widthEmu, int heightEmu);
    void inject_drawing_anchor(int imgSeq, int widthEmu, int heightEmu, bool behindDoc);
};

// TableCell contains one or more paragraphs
class TableCell {
  private:
    friend class IteratorHelper;
    pugi::xml_node parent;
    pugi::xml_node current;

    Paragraph paragraph;

  public:
    TableCell();
    TableCell(pugi::xml_node, pugi::xml_node);

    void set_parent(pugi::xml_node);
    void set_current(pugi::xml_node);

    Paragraph &paragraphs();

    TableCell &next();
    bool has_next() const;
};

// TableRow consists of one or more TableCells
class TableRow {
    friend class IteratorHelper;
    pugi::xml_node parent;
    pugi::xml_node current;

    TableCell cell;

  public:
    TableRow();
    TableRow(pugi::xml_node, pugi::xml_node);
    void set_parent(pugi::xml_node);
    void set_current(pugi::xml_node);

    TableCell &cells();

    bool has_next() const;
    TableRow &next();
};

// Table consists of one or more TableRow objects
class Table {
  private:
    friend class IteratorHelper;
    pugi::xml_node parent;
    pugi::xml_node current;

    TableRow row;

  public:
    Table();
    Table(pugi::xml_node, pugi::xml_node);
    void set_parent(pugi::xml_node);
    void set_current(pugi::xml_node);

    Table &next();
    bool has_next() const;

    TableRow &rows();
};

// Document contains whole the docx file
// and stores paragraphs
class Document {
  private:
    friend class IteratorHelper;
    std::string directory;
    Paragraph paragraph;
    Table table;
    pugi::xml_document document;
    bool flag_is_open;

  public:
    Document();
    Document(std::string);
    void file(std::string);
    void open();
    void save() const;
    bool is_open() const;

    Paragraph &paragraphs();
    Table &tables();
};
} // namespace duckx

#endif
