#include <unistd.h>

#include <cassert>
#include <cstdio>

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <strings.h>
#include <libGDSII.h>

#include "ps-template.ps.rawstring"

static constexpr float kDefaultScale = 30000;
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
          "\t-l <layer[,layer...]> : choose layers (allowed multiple times)\n"
          "\t-o <file>  : output filename (otherwise: stdout)\n"
          "\t-s <scale> : output scale (default: %.0f)\n"
          "\t-t <title> : Title on base-plate\n",
          progname, kDefaultScale);
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

void WritePostscript(FILE *out, const char *title, float output_scale,
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
    fprintf(out, "() %.3f %.3f %.3f %.3f show-bounding-box\n",
            bounding_box.p0.x,
            bounding_box.p0.y, bounding_box.width(), bounding_box.height());

    // Emit the elements for this layer.
    for (const auto &s : gds.Structs) {
      for (const auto &e : s->Elements) {
        if (e->Layer != layer) continue;
        fprintf(out, "%% datatype=%d\n", e->DataType);
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
        else if (e->Type == PATH) {
          // A path is just a polygon around the line. We should be
          // perpendicular to the path then create lines there. For now,
          // just make it a very simple polygon and assume it is horizontal
          // (Used in the skywater power supply rails)
          if (e->XY.size() != 4) {
            std::cerr << "Oops, can't deal with multi-vertex path yet "
                      << e->XY.size() << "\n";
            continue;
          }
          const double width = e->Width * unit_factor;
          fprintf(out, "%%%% Path of width %.2f\n", width);
          const Point p0 = { e->XY[0] * unit_factor, e->XY[1] * unit_factor };
          const Point p1 = { e->XY[2] * unit_factor, e->XY[3] * unit_factor };
          fprintf(out, "%.4f %.4f moveto\n", p0.x, p0.y - width/2);
          fprintf(out, "%.4f %.4f lineto\n", p1.x, p1.y - width/2);
          fprintf(out, "%.4f %.4f lineto\n", p1.x, p1.y + width/2);
          fprintf(out, "%.4f %.4f lineto\n", p0.x, p0.y + width/2);
          fprintf(out, "closepath stroke\n");
        }
        else {
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

  // Create the backplane
  fprintf(out, "%s Backplane %d\n", "%%Page:", page++);
  fprintf(out, "%.3f %.3f %d start-page\n", -bounding_box.p0.x,
          -bounding_box.p0.y, 1000);
  fprintf(out, "(%s @ %.0f:1 scale) %.3f %.3f %.3f %.3f show-bounding-box\n",
          title, output_scale,
          bounding_box.p0.x,
          bounding_box.p0.y, bounding_box.width(), bounding_box.height());
  fprintf(out, "%.4f %.4f moveto ( %.0f nm ) %.4f %.4f hor-measure-line\n",
          bounding_box.p0.x, bounding_box.p0.y,
          bounding_box.width() * 1000,
          bounding_box.width() * 0.01, bounding_box.width());
  fprintf(out, "%.4f %.4f moveto ( %.0f nm ) %.4f %.4f ver-measure-line\n",
          bounding_box.p1.x, bounding_box.p0.y,
          bounding_box.height() * 1000,
          bounding_box.height() * 0.01, bounding_box.height());

  fprintf(out, "showpage\n");

  // Create a bunch of pins. Pro-tip: peel paper from Acrylic
  // before cutting.
  fprintf(out, "%s Pins %d\n", "%%Page:", page++);
  fprintf(out, "%.3f %.3f %d start-page\n", -bounding_box.p0.x,
          -bounding_box.p0.y, 1001);
  const int pin_datatype = 44;
  fprintf(out, "%s setrgbcolor\n", datatype_color[pin_datatype]);
  const float pin_size = 0.17;  // Note: hardcoded for skywater observation.
  const int grid = 8;
  for (int x = 0; x <= grid; ++x) {
    fprintf(out, "%.4f %.4f moveto 0 %.4f rlineto stroke\n",
            x * pin_size, -pin_size/4, (grid + 0.5) * pin_size);
  }
  for (int y = 0; y <= grid; ++y) {
    fprintf(out, "%.4f %.4f moveto %.4f 0 rlineto stroke\n",
            -pin_size/4, y * pin_size, (grid + 0.5) * pin_size);
  }
  fprintf(out, "showpage\n");
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
  const char *title = nullptr;

  int opt;
  while ((opt = getopt(argc, argv, "hl:o:s:t:")) != -1) {
    switch (opt) {
    case 'h': return usage(argv[0]);
    case 'l': SetAppend(optarg, &selected_layers); break;
    case 'o': out = fopen(optarg, "wb"); break;
    case 's': output_scale = atof(optarg); break;
    case 't': title = optarg; break;
    default:
      return usage(argv[0]);
    }
  }

  const std::string command = argv[optind++];
  const char *gds_filename = argv[optind];
  if (!title) title = gds_filename;

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
    WritePostscript(out, title, output_scale, selected_layers, gds);
  } else {
    fprintf(stderr, "Unknown command\n");
    return usage(argv[0]);
  }

  return 0;
}
