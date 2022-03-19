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
          "Command is one of\n"
          "\tps      : output postscript\n"
          "\tlayers  : print layers\n"
          "\tdesc    : print description of content\n"
          "Options\n"
          " -h         : help\n"
          " -l <layer> : choose a specific layer\n",
          progname);
  return 1;
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    return usage(argv[0]);
  }

  FILE *out = stdout;
  int selected_layer = -1;
  int opt;
  while ((opt = getopt(argc, argv, "hl:o:")) != -1) {
    switch (opt) {
    case 'h': return usage(argv[0]);
    case 'l': selected_layer = atoi(optarg); break;
    case 'o': out = fopen(optarg, "wb"); break;
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
    int color = 0;
    fwrite(kps_template_ps, sizeof(kps_template_ps) - 1, 1, out);
    for (const int layer : gds.GetLayers()) {
      if (selected_layer >= 0 && selected_layer != layer)
        continue;
      fprintf(out, "%% Layer %d\n%s setrgbcolor\n", layer, kColors[color]);
      color = (color + 1) % kColors.size();
      for (const dVec& vertices : gds.GetPolygons(layer)) {
        assert(vertices.size() % 2 == 0);

        // set color according to layer.
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
    }
      fprintf(out, "showpage\n");
  } else {
    fprintf(stderr, "Unknown command\n");
    return usage(argv[0]);
  }

  return 0;
}
