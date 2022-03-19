#include <unistd.h>

#include <cassert>
#include <cstdio>

#include <string>

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
          "\t-l <layer> : choose a specific layer\n"
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

int main(int argc, char *argv[]) {
  if (argc < 3) {
    return usage(argv[0]);
  }

  FILE *out = stdout;
  int selected_layer = -1;
  float output_scale = 20000.0;

  int opt;
  while ((opt = getopt(argc, argv, "hl:o:s:")) != -1) {
    switch (opt) {
    case 'h': return usage(argv[0]);
    case 'l': selected_layer = atoi(optarg); break;
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
    Box bounding_box;
    for (const dVec& vertices : gds.GetPolygons()) {
      UpdateBoundindBox(vertices, &bounding_box);
    }
    const float mm_points = 72/25.4;
    float factor = output_scale * 0.001 / 25.4 * 72;
    fprintf(out, "%%!PS-Adobe-2.0\n"
            "%%%%BoundingBox: 0 0 %.0f %.0f\n\n",
            bounding_box.width() * factor + 20 * mm_points,
            bounding_box.height() * factor + 30 * mm_points);

    // TODO:micrometer to real bounding box
    fprintf(out, "/display-scale %.0f def  %% 1 micrometer -> %.0f mm\n",
            output_scale, output_scale / 1000.0);

    fwrite(kps_template_ps, sizeof(kps_template_ps) - 1, 1, out);

    int page = 0;
    for (const int layer : gds.GetLayers()) {
      if (selected_layer >= 0 && selected_layer != layer)
        continue;
      fprintf(out, "%s Layer-%d %d\n", "%%Page:", layer, page++);
      fprintf(out, "%.3f %.3f %d start-page\n",
              -bounding_box.p0.x, -bounding_box.p0.y, layer);
      fprintf(out, "%.3f %.3f %.3f %.3f show-bounding-box\n",
              bounding_box.p0.x, bounding_box.p0.y, bounding_box.width(),
              bounding_box.height());

      // TODO: set color depending on datattype
      fprintf(out, "%s setrgbcolor\n", kColors[0]);

      for (const dVec& vertices : gds.GetPolygons(layer)) {
        assert(vertices.size() % 2 == 0);

        bool is_first = true;
        dVec::const_iterator it = vertices.begin();
        while (it != vertices.end()) {
          double x = *it++;
          double y = *it++;
          fprintf(out, "%.4f %.4f %s\n", x, y, is_first ? "moveto" : "lineto");
          is_first = false;
        }
        fprintf(out, "closepath stroke\n\n");
      }

      fprintf(out, "%s setrgbcolor\n", kColors[1]);
      for (const TextString& text : gds.GetTextStrings(layer)) {
        assert(text.XY.size() == 2);
        fprintf(out, "%.3f %.3f (%s) center-text\n",
                text.XY[0], text.XY[1], text.Text);
      }
      fprintf(out, "showpage\n");
    }
  } else {
    fprintf(stderr, "Unknown command\n");
    return usage(argv[0]);
  }

  return 0;
}
