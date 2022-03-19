#include <unistd.h>

#include <cassert>
#include <cstdio>

#include <string>
#include <set>
#include <map>

#include <libGDSII.h>

#include "ps-template.ps.rawstring"

static std::vector<const char *> kColors = {
    "0 0 0", "1 0 0", "0 1 0", "0 0 1", "0 1 1", "1 0 1", "1 1 0",
};

static int usage(const char *progname) {
  fprintf(stderr, "Usage: %s [options] command <gdsfile>\n"
          "[Command]\n"
          "\tps      : output postscript\n"
          "\tlayers  : show available layers\n"
          "\tdesc    : print description of content\n"
          "[Options]\n"
          "\t-h         : help\n"
          "\t-l <layer> : choose a specific layer (allowed multiple times)\n"
          "\t-o <file>  : output filename (otherwise: stdout)\n"
          "\t-s <scale> : output scale (default: 20000)\n",
          progname);
  return 1;
}

struct Point {
  float x = 0;
  float y = 0;
};
struct Box {
  Point p0;
  Point p1;
  float width() const { return p1.x - p0.x; }
  float height() const { return p1.y - p0.y; }
};

void UpdateBoundindBox(const dVec &vertices, Box *bbox) {
  dVec::const_iterator it = vertices.begin();
  while (it != vertices.end()) {
    double x = *it++;
    double y = *it++;
    if (x < bbox->p0.x) bbox->p0.x = x;
    if (x > bbox->p1.x) bbox->p1.x = x;
    if (y < bbox->p0.y) bbox->p0.y = y;
    if (y > bbox->p1.y) bbox->p1.y = y;
  }
}

void WritePostscript(FILE *out, float output_scale,
                     const std::set<int> selected_layers,
                     libGDSII::GDSIIData &gds) {
  // Find all data types so that we can assign colors
  std::map<int, const char*> datatype_color;
  for (const auto &s : gds.Structs) {
    for (const auto &e : s->Elements) {
      datatype_color.insert({e->DataType, nullptr});
    }
  }
  size_t color = 0;
  for (auto &dc : datatype_color) {
    dc.second = kColors[color++];
    if (color >= kColors.size()) color = 0;
  }


  // Determine bounding box and start page.
  Box bounding_box;
  for (const dVec &vertices : gds.GetPolygons()) {
    UpdateBoundindBox(vertices, &bounding_box);
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

  const float unit_factor = gds.FileUnits[0];  // ? Looks like the right value
  int page = 0;
  for (const int layer : gds.GetLayers()) {
    if (!selected_layers.empty() &&
        selected_layers.find(layer) == selected_layers.end()) {
      continue;
    }

    const char *last_color = nullptr;
    fprintf(out, "%s Layer-%d %d\n", "%%Page:", layer, page++);
    fprintf(out, "%.3f %.3f %d start-page\n", -bounding_box.p0.x,
            -bounding_box.p0.y, layer);
    fprintf(out, "%.3f %.3f %.3f %.3f show-bounding-box\n", bounding_box.p0.x,
            bounding_box.p0.y, bounding_box.width(), bounding_box.height());

    // Emit the elements for this layer.
    for (const auto &s : gds.Structs) {
      for (const auto &e : s->Elements) {
        if (e->Layer != layer) continue;
        if (const char *col = datatype_color[e->DataType]; col != last_color) {
          fprintf(out, "%s setrgbcolor\n", col);
          last_color = col;
        }
        if (e->Text) {
          fprintf(out, "%.3f %.3f %.3f (%s) center-text\n",
                  e->XY[0] * unit_factor, e->XY[1] * unit_factor,
                  e->Angle,
                  e->Text->c_str());
        }
        else if (e->Type != PATH) {
          // Polygon.
          bool is_first = true;
          iVec::const_iterator it = e->XY.begin();
          while (it != e->XY.end()) {
            double x = *it++ * unit_factor;
            double y = *it++ * unit_factor;
            fprintf(out, "%.4f %.4f %s\n", x, y,
                    is_first ? "moveto" : "lineto");
            is_first = false;
          }
          fprintf(out, "closepath stroke\n\n");
        }
      }
    }
    fprintf(out, "showpage\n\n");
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    return usage(argv[0]);
  }

  FILE *out = stdout;
  std::set<int> selected_layers;
  float output_scale = 20000.0;

  int opt;
  while ((opt = getopt(argc, argv, "hl:o:s:")) != -1) {
    switch (opt) {
    case 'h': return usage(argv[0]);
    case 'l': selected_layers.insert(atoi(optarg)); break;
    case 'o': out = fopen(optarg, "wb"); break;
    case 's': output_scale = atof(optarg); break;
    default:
      return usage(argv[0]);
    }
  }

  const std::string command = argv[optind++];
  const char *gds_filename = argv[optind];

  libGDSII::GDSIIData gds(gds_filename);
  if (gds.ErrMsg) {
    fprintf(stderr, "Issue loading file %s: %s\n",
            gds_filename, gds.ErrMsg->c_str());
    return 1;
  }

  if (command == "layers") {
    const iVec layers = gds.GetLayers();
    fprintf(out, "layer\tpolygons\ttexts\n");
    for (int layer : layers) {
      const int polygon_count = gds.GetPolygons(layer).size();
      const int text_count = gds.GetTextStrings(layer).size();
      fprintf(out, "%-5d\t%8d\t%5d\n", layer, polygon_count, text_count);
    }
    fprintf(stderr, "%d layers\n", (int)layers.size());
  } else if (command == "desc") {
    gds.WriteDescription();
  } else if (command == "ps") {
    WritePostscript(out, output_scale, selected_layers, gds);
  } else {
    fprintf(stderr, "Unknown command\n");
    return usage(argv[0]);
  }

  return 0;
}
