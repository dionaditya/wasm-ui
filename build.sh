#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

IMGUI=imgui

emcc -O2 -c duckx/zip.c -Iduckx -o duckx/zip.o

em++ main.cpp \
  pdf/pdfdoc.cpp \
  pdf/pdfraster.cpp \
  $IMGUI/imgui.cpp \
  $IMGUI/imgui_draw.cpp \
  $IMGUI/imgui_tables.cpp \
  $IMGUI/imgui_widgets.cpp \
  $IMGUI/imgui_demo.cpp \
  $IMGUI/backends/imgui_impl_sdl2.cpp \
  $IMGUI/backends/imgui_impl_opengl3.cpp \
  duckx/duckx.cpp \
  duckx/pugixml.cpp \
  duckx/zip.o \
  -I. \
  -I$IMGUI \
  -I$IMGUI/backends \
  -Iduckx \
  -std=c++17 \
  -Wall -Wformat \
  -Os \
  -s USE_SDL=2 \
  -s USE_ZLIB=1 \
  -s DISABLE_EXCEPTION_CATCHING=1 \
  -s WASM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s NO_EXIT_RUNTIME=0 \
  -s ASSERTIONS=1 \
  -s FORCE_FILESYSTEM=1 \
  -DIMGUI_DISABLE_FILE_FUNCTIONS \
  -sFETCH=1 \
  -sEXPORTED_RUNTIME_METHODS=ccall,HEAPU8,UTF8ToString,FS \
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_doc_receive_image,_doc_paste_text,_doc_import_docx,_doc_imported_math_count,_doc_imported_math_para,_doc_imported_math_tex,_doc_imported_image_count,_doc_imported_image_para,_doc_imported_image_path,_doc_attach_image,_doc_debug_page_columns,_doc_debug_page_setup,_doc_debug_state,_doc_debug_tab,_doc_debug_insert_tab,_doc_debug_para_fmt,_doc_debug_hf,_doc_export_docx_size,_doc_export_docx_data,_doc_export_pdf_size,_doc_export_pdf_data,_doc_pdf_debug,_doc_debug_apply,_imgui_add_char,_imgui_add_key,_imgui_want_text_input,_pdf_open_document,_pdf_open_file,_pdf_render_page,_pdf_page_count,_pdf_doc_debug,_pdf_open_picker,_pdf_selected_debug,_pdf_text_item_count \
  -sSTACK_SIZE=2097152 \
  --preload-file template.docx \
  --preload-file assets/compressed.tracemonkey-pldi-09.pdf@/compressed.tracemonkey-pldi-09.pdf \
  --shell-file shell.html \
  -o index.html

echo "Build complete."
