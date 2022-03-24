#include <unistd.h>

#include <cassert>
#include <cstdio>

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <strings.h>

#include "gds-query.h"

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

struct BoundingBox {
  Point p0;
  Point p1;

  void Update(const vector<Point>& vertices) {
    for (const Point &point : vertices) {
      if (!initialized || point.x < p0.x) p0.x = point.x;
      if (!initialized || point.x > p1.x) p1.x = point.x;
      if (!initialized || point.y < p0.y) p0.y = point.y;
      if (!initialized || point.y > p1.y) p1.y = point.y;
      initialized = true;
    }
  }
  float width() const { return p1.x - p0.x; }
  float height() const { return p1.y - p0.y; }
  bool initialized = false;
};

void WritePostscript(FILE *out, const char *title, float output_scale,
                     const std::set<int> selected_layers,
                     const GDSQuery &gds) {
  // Create a color mapping.
  std::map<int, const char*> datatype_color;
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

  const int pin_datatype = 44; // hardcoded sky130 observed.
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
            -bounding_box.p0.x,
            -bounding_box.p0.y, layer, output_scale);
    fprintf(out, "() %.3f %.3f %.3f %.3f show-bounding-box\n",
            bounding_box.p0.x,
            bounding_box.p0.y, bounding_box.width(), bounding_box.height());

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
      fprintf(out, "%.3f %.3f %.3f (%s) center-text\n",
              text.position.x, text.position.y, text.angle, text.text);
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
      bool is_first = true;
      for (const Point &vertex : polygon.vertices) {
        fprintf(out, "%.4f %.4f %s\n", vertex.x, vertex.y,
                is_first ? "moveto" : "lineto");
        is_first = false;
      }
      fprintf(out, "closepath stroke\n\n");
    }

    fprintf(out, "showpage\n\n");
  }

  // Create the backplane
  fprintf(out, "%s Backplane %d\n", "%%Page:", page++);
  fprintf(out, "%.3f %.3f (Backplane acrylic; also cool if engraved mirrored) start-page\n",
          -bounding_box.p0.x, -bounding_box.p0.y);

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

  fprintf(out, "showpage\n\n");

  if (pin_count) {
    // Create a bunch of pins. Pro-tip: peel paper from Acrylic before cutting.
    fprintf(out, "%s Pins %d\n", "%%Page:", page++);
    const int grid = ceil(sqrt(2 * pin_count));
    fprintf(out, "%.3f %.3f (%d Bulk Pins, seen %d; at least twice as many - "
            "some need to be stacked. Peel before cut...) start-page\n", -bounding_box.p0.x,
            -bounding_box.p0.y, grid*grid, pin_count);

    fprintf(out, "%s setrgbcolor\n", datatype_color[pin_datatype]);
    if (observed_pin_width.size() > 1) {
      fprintf(stderr, "Seen multiple sizes, just creating smallest\n");
      for (float w : observed_pin_width) {
        fprintf(stderr, "%.06f\n", w);
      }
    }
    const float pin_size = *observed_pin_width.begin();
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

  GDSQuery gds;
  if (!gds.Load(gds_filename)) {
    return 1;
  }

  if (command == "layers") {
    const auto& layers = gds.GetLayers();
    fprintf(out, "layer\tdatatype\tpolygons\ttexts\n");
    for (int layer : layers) {
      for (int datatype : gds.GetDatatypes(layer)) {
        const int polygon_count = gds.FindPolygons(layer, datatype).size();
        const int text_count = gds.FindTexts(layer, datatype).size();
        fprintf(out, "%-5d\t%8d\t%8d\t%5d\n", layer, datatype,
                polygon_count, text_count);
      }
    }
    fprintf(stderr, "%d layers\n", (int)layers.size());
  } else if (command == "desc") {
    gds.PrintDescription();
  } else if (command == "ps") {
    WritePostscript(out, title, output_scale, selected_layers, gds);
  } else {
    fprintf(stderr, "Unknown command\n");
    return usage(argv[0]);
  }

  return 0;
}
