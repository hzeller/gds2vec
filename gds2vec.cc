#include <strings.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include "gds-query.h"
#include "ps-template.ps.rawstring"

static constexpr float kDefaultScale = 30000;
static std::vector<const char *> kColors = {
  "0 0 0", "1 0 0", "0 1 0", "0 0 1", "0 1 1", "1 0 1", "1 1 0",
};

static int usage(const char *progname) {
  fprintf(stderr,
          "Usage: %s [options] command <gdsfile>\n"
          "[Command]\n"
          "\tsky130  : output laser-cut files for sky130 standard cells\n"
          "\tps      : output chosen layers (as PostScript)\n"
          "\tlayers  : show available layers\n"
          "\tdesc    : print description of content\n"
          "[Options]\n"
          "\t-h         : help\n"
          "\t-l <layer[,layer...]> : choose layers (allowed multiple times)\n"
          "\t-o <file>  : output filename (otherwise: stdout)\n"
          "\t-s <scale> : output scale (default: %.0f)\n"
          "\t-t <title> : Title on base-plate\n",
          progname, kDefaultScale);
  return 1;
}

void PostscriptPrintPolygon(FILE *out, const std::vector<Point> &vertices) {
  bool is_first = true;
  for (const Point &vertex : vertices) {
    fprintf(out, "%.4f %.4f %s\n", vertex.x, vertex.y,
            is_first ? "moveto" : "lineto");
    is_first = false;
  }
  fprintf(out, "closepath stroke\n\n");
}

void PostscriptBackplane(FILE *out, std::string_view title, int page,
                         float output_scale,
                         const BoundingBox &bounding_box) {
  fprintf(out, "%s Backplane %d\n", "%%Page:", page);
  fprintf(out,
          "%.3f %.3f (Backplane acrylic; also cool if engraved mirrored) "
          "start-page\n",
          -bounding_box.p0.x, -bounding_box.p0.y);

  fprintf(out, "(%.*s @ %.0f:1 scale) %.3f %.3f %.3f %.3f cut-color show-bounding-box\n",
          (int)title.length(), title.data(),
          output_scale, bounding_box.p0.x, bounding_box.p0.y,
          bounding_box.width(), bounding_box.height());
  fprintf(out, "%.4f %.4f moveto ( %.0f nm ) %.4f %.4f hor-measure-line\n",
          bounding_box.p0.x, bounding_box.p0.y, bounding_box.width() * 1000,
          bounding_box.width() * 0.01, bounding_box.width());
  fprintf(out, "%.4f %.4f moveto ( %.0f nm ) %.4f %.4f ver-measure-line\n",
          bounding_box.p1.x, bounding_box.p0.y, bounding_box.height() * 1000,
          bounding_box.height() * 0.01, bounding_box.height());

  fprintf(out, "showpage\n\n");
}

// Create a printout useful to laser-cut standard cells from the Sky130 PDK.
void Sky130LayoutCut(FILE *out, std::string_view title,
                     float output_scale, const GDSQuery &gds) {
  // Special meanings: negative layers: subtract.
  // Datatype*1000: bounding box. So 2000 is bounding box of datatype 20.
  struct Page {
    enum { Cardboard, Acrylic } type;
    const char *title;
    std::vector<LayeredElement> elements;
  };
  // clang-format off
  const Page pages[] = {
    // Glue alignment templates made in cardboard
    {Page::Cardboard, "nwell+diff",                  {{64, 20}, {65, 20}}},
    {Page::Cardboard, "poly",                        {{66, 20}}},
    {Page::Cardboard, "nwell,diff,poly -> LI",       {{67, 2000},{66, 44}}},
    {Page::Cardboard, "local interconnect LI",       {{67, 20}}},
    {Page::Cardboard, "LI -> Metal1",                {{67, 44}}},
    {Page::Cardboard, "Metal1",                      {{68, 20}}},

    // The actual acrylic.
    {Page::Acrylic,   "nwell-diff [green]",          {{64, 20}, {-65, 20}}},
    {Page::Acrylic,   "diff [lightblue]",            {{65, 20}}},
    {Page::Acrylic,   "poly [orange]",               {{66, 20}}},
    {Page::Acrylic,   "Local Interconnect [yellow]", {{67, 20}}},

    // A layer that takes the LI outline and provides slots for pins from below.
    {Page::Acrylic,   "LI-support [transparent]",    {{67, 2000},{66, 44}}},
    {Page::Acrylic,   "Metal1",                      {{68, 20}}},
  };
  // clang-format on

  const int pin_datatype = 44;  // hardcoded sky130 observed.
  const float pin_size = 0.17;

  // Determine bounding box and start page.
  BoundingBox bounding_box;
  for (const auto &polygon : gds.FindPolygons()) {
    bounding_box.Update(polygon.vertices);
  }
  const float mm_points = 72 / 25.4;
  const float factor = output_scale * 0.001 / 25.4 * 72;
  fprintf(out,
          "%%!PS-Adobe-2.0\n"
          "%%%%BoundingBox: 0 0 %.0f %.0f\n\n",
          bounding_box.width() * factor + 20 * mm_points,
          bounding_box.height() * factor + 30 * mm_points);

  fprintf(out, "/display-scale %.0f def  %% 1 micrometer -> %.0f mm\n",
          output_scale, output_scale / 1000.0);

  // Postscript boilerplate.
  fwrite(kps_template_ps, sizeof(kps_template_ps) - 1, 1, out);

  int page_num = 0;
  int pin_count = 0;
  for (const Page &page : pages) {
    page_num++;
    fprintf(out, "\n%s %d %d\n", "%%Page:", page_num, page_num);
    fprintf(out, "%.3f %.3f (%s %s %.0f:1 scale) start-page\n",
            -bounding_box.p0.x, -bounding_box.p0.y,
            (page.type == Page::Cardboard) ? "Cardboard template" : "Acrylic",
            page.title,
            output_scale);
    fprintf(out, "(%s) %.3f %.3f %.3f %.3f %s show-bounding-box\n",
            (page.type == Page::Cardboard) ? page.title : "",
            bounding_box.p0.x, bounding_box.p0.y, bounding_box.width(),
            bounding_box.height(),
            (page.type == Page::Cardboard) ? "cut-color" : "comment-color");
    for (const LayeredElement &e : page.elements) {
      const int layer = abs(e.layer);  // TODO: implement subtract

      if (page.type == Page::Acrylic && e.datatype < 1000) {
        fprintf(out, "scan-color setrgbcolor\n");
        for (const auto &text : gds.FindTexts(layer)) {
          fprintf(out, "%.3f %.3f %.3f (%s) center-text\n", text.position.x,
                  text.position.y, text.angle, text.text);
        }

        if (e.layer > 0) {
          fprintf(out, "engrave-color setrgbcolor\n");
          for (const auto &polygon : gds.FindPolygons(layer, 16)) {
            PostscriptPrintPolygon(out, polygon.vertices);
          }
        }
      }

      fprintf(out, "cut-color setrgbcolor\n");

      if (e.datatype > 1000) {  // special polygon: the bounding box.
        const int datatype = e.datatype / 100;
        BoundingBox datatype_bbox;
        for (const auto &polygon : gds.FindPolygons(layer, datatype)) {
          datatype_bbox.Update(polygon.vertices);
        }
        PostscriptPrintPolygon(out, datatype_bbox.vertices());
      } else {
        for (const auto &polygon : gds.FindPolygons(layer, e.datatype)) {
          fprintf(out, "%% datatype=%d\n", polygon.datatype);
          PostscriptPrintPolygon(out, polygon.vertices);
          if (page.type == Page::Cardboard && e.datatype == pin_datatype) {
            ++pin_count;  // Remember for later
          }
        }
      }
    }
    fprintf(out, "showpage\n\n");
  }

  // Create the backplane
  PostscriptBackplane(out, title, ++page_num, output_scale, bounding_box);

  if (pin_count) {
    // Create a bunch of pins. Pro-tip: peel paper from Acrylic before cutting.
    fprintf(out, "%s Pins %d\n", "%%Page:", ++page_num);
    const int grid = ceil(sqrt(2 * pin_count));
    fprintf(out,
            "%.3f %.3f (%d Bulk Pins, seen %d; at least twice as many - "
            "some need to be stacked. Peel before cut...) start-page\n",
            -bounding_box.p0.x, -bounding_box.p0.y, grid * grid, pin_count);

    // First horizontal cuts, so that they don't fall through vertical grates
    fprintf(out, "cut-color setrgbcolor\n");
    for (int y = 0; y <= grid; ++y) {
      fprintf(out, "%.4f %.4f moveto %.4f 0 rlineto stroke\n", -pin_size / 4,
              y * pin_size, (grid + 0.5) * pin_size);
    }

    // Now, next DXF layer, cut the pins free
    fprintf(out, "cut-color2 setrgbcolor\n");
    for (int x = 0; x <= grid; ++x) {
      fprintf(out, "%.4f %.4f moveto 0 %.4f rlineto stroke\n", x * pin_size,
              -pin_size / 4, (grid + 0.5) * pin_size);
    }

    fprintf(out, "showpage\n");
  }
}

void WritePostscript(FILE *out, std::string_view title, float output_scale,
                     const std::set<int> selected_layers, const GDSQuery &gds) {
  // Create a color mapping.
  std::map<int, const char *> datatype_color;
  size_t color = 0;
  for (int d : gds.GetDatatypes()) {
    datatype_color.insert({d, kColors[color++]});
    if (color >= kColors.size()) color = 0;
  }

  // Determine bounding box and start page.
  BoundingBox bounding_box;
  for (const auto &polygon : gds.FindPolygons()) {
    bounding_box.Update(polygon.vertices);
  }
  const float mm_points = 72 / 25.4;
  const float factor = output_scale * 0.001 / 25.4 * 72;
  fprintf(out,
          "%%!PS-Adobe-2.0\n"
          "%%%%BoundingBox: 0 0 %.0f %.0f\n\n",
          bounding_box.width() * factor + 20 * mm_points,
          bounding_box.height() * factor + 30 * mm_points);

  fprintf(out, "/display-scale %.0f def  %% 1 micrometer -> %.0f mm\n",
          output_scale, output_scale / 1000.0);

  // Postscript boilerplate.
  fwrite(kps_template_ps, sizeof(kps_template_ps) - 1, 1, out);

  const int pin_datatype = 44;  // hardcoded sky130 observed.
  int pin_count = 0;
  std::set<float> observed_pin_width;

  int page = 0;
  for (const int layer : gds.GetLayers()) {
    if (!selected_layers.empty() &&
        selected_layers.find(layer) == selected_layers.end()) {
      continue;
    }

    fprintf(out, "%s Layer-%d %d\n", "%%Page:", layer, page++);
    fprintf(out, "%.3f %.3f (Layer %d @ %.0f:1 scale) start-page\n",
            -bounding_box.p0.x, -bounding_box.p0.y, layer, output_scale);
    fprintf(out, "() %.3f %.3f %.3f %.3f comment-color show-bounding-box\n",
            bounding_box.p0.x, bounding_box.p0.y, bounding_box.width(),
            bounding_box.height());

    const char *last_col = nullptr;
    const auto change_color = [&](int datatype) {
      if (const char *col = datatype_color[datatype]; col != last_col) {
        fprintf(out, "%s setrgbcolor\n", col);
        last_col = col;
      }
    };

    for (const auto &text : gds.FindTexts(layer)) {
      fprintf(out, "%% datatype=%d\n", text.datatype);
      change_color(text.datatype);
      fprintf(out, "%.3f %.3f %.3f (%s) center-text\n", text.position.x,
              text.position.y, text.angle, text.text);
    }

    for (const auto &polygon : gds.FindPolygons(layer)) {
      fprintf(out, "%% datatype=%d\n", polygon.datatype);
      change_color(polygon.datatype);
      if (polygon.datatype == pin_datatype) {
        BoundingBox pin;
        pin.Update(polygon.vertices);
        observed_pin_width.insert(lround(1000 * pin.width()) / 1000.0);
        ++pin_count;
      }
      PostscriptPrintPolygon(out, polygon.vertices);
    }

    fprintf(out, "showpage\n\n");
  }

  PostscriptBackplane(out, title, page, output_scale, bounding_box);
}

static void SetAppend(const char *str, std::set<int> *out) {
  while (str && *str) {
    out->insert(atoi(str));
    str = index(str, ',');
    if (!str) break;
    ++str;  // Place after comma.
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    return usage(argv[0]);
  }

  FILE *out = stdout;
  std::set<int> selected_layers;
  float output_scale = kDefaultScale;
  std::string_view title;

  int opt;
  while ((opt = getopt(argc, argv, "hl:o:s:t:")) != -1) {
    switch (opt) {
    case 'h': return usage(argv[0]);
    case 'l': SetAppend(optarg, &selected_layers); break;
    case 'o': out = fopen(optarg, "wb"); break;
    case 's': output_scale = atof(optarg); break;
    case 't': title = optarg; break;
    default: return usage(argv[0]);
    }
  }

  const std::string command = argv[optind++];
  const char *gds_filename = argv[optind];
  if (title.empty()) {
    // Make some reasonable title string from the filename.
    title = gds_filename;
    if (auto slash = title.find_last_of('/'); slash != std::string::npos) {
      title = title.substr(slash + 1);
    }
    if (auto dot = title.find_last_of('.'); dot != std::string::npos) {
      title = title.substr(0, dot);
    }
  }

  GDSQuery gds;
  if (!gds.Load(gds_filename)) {
    return 1;
  }

  if (command == "layers") {
    const auto &layers = gds.GetLayers();
    fprintf(out, "layer\tdatatype\tpolygons\ttexts\n");
    for (int layer : layers) {
      for (int datatype : gds.GetDatatypes(layer)) {
        const int polygon_count = gds.FindPolygons(layer, datatype).size();
        const int text_count = gds.FindTexts(layer, datatype).size();
        fprintf(out, "%-5d\t%8d\t%8d\t%5d\n", layer, datatype, polygon_count,
                text_count);
      }
    }
    fprintf(stderr, "%d layers\n", (int)layers.size());
  } else if (command == "desc") {
    gds.PrintDescription();
  } else if (command == "ps") {
    WritePostscript(out, title, output_scale, selected_layers, gds);
  } else if (command == "sky130") {
    Sky130LayoutCut(out, title, output_scale, gds);
  } else {
    fprintf(stderr, "Unknown command\n");
    return usage(argv[0]);
  }

  return 0;
}
