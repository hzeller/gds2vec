// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#include <iostream>
#include <memory>
#include <vector>
#include <set>

#include <libGDSII.h>

struct Point {
  double x = 0;
  double y = 0;
};

// Little facade API for what we need from the GDS file to hide the slightly
// awkward interface of libgdsii.
// Will also provide a good starting point in case we switch libs.
class GDSQuery {
public:
  struct LayeredElement {
    int layer = 0;
    int datatype = 0;
  };

  struct TextInfo : public LayeredElement {
    Point position;
    double angle;
    const char *text;
  };

  struct Polygon : public LayeredElement {
    std::vector<Point> vertices;
  };

  GDSQuery() {}

  bool Load(const char *filename) {
    gds_.reset(new libGDSII::GDSIIData(filename));
    if (gds_->ErrMsg) {
      fprintf(stderr, "Issue loading file %s: %s\n",
              filename, gds_->ErrMsg->c_str());
      return false;
    }
    return true;
  }

  void PrintDescription() {
    gds_->WriteDescription();
  }

  // Get list of all available layers.
  std::vector<int> GetLayers() const { return gds_->GetLayers(); }

  std::set<int> GetDatatypes(int layer = -1) const {
    std::set<int> result;
    for (const auto &s : gds_->Structs) {
      for (const auto &e : s->Elements) {
        if (layer >= 0 && e->Layer != layer) continue;
        result.insert(e->DataType);
      }
    }
    return result;
  }

  std::vector<TextInfo> FindTexts(int layer = -1, int datatype = -1) const {
    std::vector<TextInfo> result;

    const float unit_factor = gds_->FileUnits[0];  // ? Looks like right value

    for (const auto &s : gds_->Structs) {
      for (const auto &e : s->Elements) {
        if (layer >= 0 && e->Layer != layer) continue;
        if (datatype >= 0 && e->DataType != datatype) continue;
        if (e->Text) {
          TextInfo t;
          t.layer = e->Layer;
          t.datatype = e->DataType;
          t.position = {e->XY[0] * unit_factor, e->XY[1] * unit_factor};
          t.angle = e->Angle;
          t.text = e->Text->c_str();
          result.push_back(t);
        }
      }
    }
    return result;
  }

  // Return polygons for the specified layer and datatype.
  std::vector<Polygon> FindPolygons(int layer = -1, int datatype = -1) const {
    std::vector<Polygon> result;

    const float unit_factor = gds_->FileUnits[0];  // ? Looks like right value

    // Since libgdsii does not expose the datatype, we have to do the
    // filtering ourselves by poking in the low-level.
    for (const auto &s : gds_->Structs) {
      for (const auto &e : s->Elements) {
        if (layer >= 0 && e->Layer != layer) continue;
        if (datatype >= 0 && e->DataType != datatype) continue;
        Polygon polygon;
        polygon.layer = e->Layer;
        polygon.datatype = e->DataType;

        if (e->Type == PATH) {
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
          const Point p0 = { e->XY[0] * unit_factor, e->XY[1] * unit_factor };
          const Point p1 = { e->XY[2] * unit_factor, e->XY[3] * unit_factor };
          polygon.vertices.push_back({p0.x, p0.y - width/2});
          polygon.vertices.push_back({p1.x, p1.y - width/2});
          polygon.vertices.push_back({p1.x, p1.y + width/2});
          polygon.vertices.push_back({p0.x, p0.y + width/2});

          result.push_back(polygon);
        }
        else {
          iVec::const_iterator it = e->XY.begin();
          while (it != e->XY.end()) {
            Point p;
            p.x = *it++ * unit_factor;
            p.y = *it++ * unit_factor;
            polygon.vertices.push_back(p);
          }
          result.push_back(polygon);
        }
      }
    }
    return result;
  }

private:
  std::unique_ptr<libGDSII::GDSIIData> gds_;
};
