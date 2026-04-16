/******************************************************************************
 *
 * Project:  OGR 12da Driver
 * Purpose:  Definition of classes for OGR 12d Model ASCII driver.
 * Author:   kjarada
 *
 ******************************************************************************
 * Copyright (c) 2026, kjarada
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#ifndef OGR_12DA_H_INCLUDED
#define OGR_12DA_H_INCLUDED

#include "ogrsf_frmts.h"

#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

/************************************************************************/
/*                        12da Data Structures                         */
/************************************************************************/

struct Attr12da;

using AttrGroup12da = std::vector<Attr12da>;

struct Attr12da
{
    enum class Type
    {
        Group,
        Real,
        Integer,
        Text
    };

    Type eType = Type::Text;
    std::string osName;
    double dfRealValue = 0.0;
    int64_t nIntValue = 0;
    std::string osTextValue;
    std::vector<Attr12da> aoChildren;  // For Group type
};

struct Point12da
{
    double dfE = 0.0;
    double dfN = 0.0;
    std::optional<double> odfZ;
    std::string osStyle;
    std::string osColour;
    std::string osPid;
    std::vector<Attr12da> aoAttributes;
    bool bIsBreaklinePoint = false;
    bool bIsTinFrame = false;
};

struct TextEnt12da
{
    std::string osLabel;
    double dfE = 0.0;
    double dfN = 0.0;
    std::optional<double> odfZ;
    std::string osStyle;
    std::string osColour;
    std::vector<Attr12da> aoAttributes;
    std::optional<double> odfWorldsize;
    std::string osTextstyle;
    std::optional<double> odfAngle;
    std::optional<double> odfXFactor;
};

struct HorizontalIP12da
{
    double dfX = 0.0;
    double dfY = 0.0;
    std::optional<double> odfRadius;
};

struct VerticalIP12da
{
    double dfChainage = 0.0;
    double dfElevation = 0.0;
    std::optional<double> odfLength;
    std::optional<double> odfKValue;
};

struct GeometrySegment12da
{
    enum class Type
    {
        Straight,
        Arc,
        Spiral,
        Parabola
    };

    Type eType = Type::Straight;
    double dfRadius = 0.0;
    bool bMajor = false;
    // Spiral params
    double dfL1 = 0.0;
    double dfL2 = 0.0;
    double dfR1 = 0.0;
    double dfR2 = 0.0;
    // Parabola params
    double dfChainage = 0.0;
    double dfHeight = 0.0;
};

struct SuperAlignment12da
{
    std::string osName;
    std::optional<double> odfChainage;
    std::string osColour;
    std::string osStyle;
    std::vector<HorizontalIP12da> aoHorizontalIPs;
    std::vector<VerticalIP12da> aoVerticalIPs;
    std::vector<Point12da> aoHorizontalCoords;
    std::vector<std::pair<double, double>> aoVerticalCoords;  // chainage, elevation
    std::vector<GeometrySegment12da> aoHorizontalGeometry;
    std::vector<GeometrySegment12da> aoVerticalGeometry;
    std::vector<Point12da> aoGeometry3D;
};

struct Model12da
{
    std::vector<Point12da> aoPts;
    std::vector<TextEnt12da> aoTexts;
    std::vector<std::vector<Point12da>> aoPolylines;
    std::vector<std::vector<Point12da>> aoPolygons;
    std::vector<std::array<Point12da, 3>> aoTriangles;
    std::vector<std::array<Point12da, 3>> aoTrimeshes;
    std::vector<SuperAlignment12da> aoSuperAlignments;
    std::vector<Attr12da> aoAttributes;
};

/************************************************************************/
/*                           Layer Type Enum                           */
/************************************************************************/

enum class OGR12daLayerType
{
    Points,
    Lines,
    Polygons,
    TIN,
    Trimesh,
    Text,
    SuperAlignments
};

/************************************************************************/
/*                            OGR12daLayer                             */
/************************************************************************/

class OGR12daDataSource;

class OGR12daLayer final : public OGRLayer
{
    OGR12daDataSource *m_poDS = nullptr;
    OGRFeatureDefn *m_poFeatureDefn = nullptr;
    OGR12daLayerType m_eType;
    const std::string m_osModelName;
    const Model12da *m_poModel = nullptr;
    GIntBig m_nNextFID = 0;
    bool m_bEOF = false;

    OGRFeature *GetPointFeature(GIntBig nIdx);
    OGRFeature *GetLineFeature(GIntBig nIdx);
    OGRFeature *GetPolygonFeature(GIntBig nIdx);
    OGRFeature *GetTINFeature(GIntBig nIdx);
    OGRFeature *GetTrimeshFeature(GIntBig nIdx);
    OGRFeature *GetTextFeature(GIntBig nIdx);
    OGRFeature *GetSuperAlignmentFeature(GIntBig nIdx);

    void BuildPointSchema();
    void BuildLineSchema();
    void BuildPolygonSchema();
    void BuildTINSchema();
    void BuildTrimeshSchema();
    void BuildTextSchema();
    void BuildSuperAlignmentSchema();

    static OGRGeometry *MakePoint(const Point12da &pt);
    static OGRGeometry *MakeLineString(const std::vector<Point12da> &aoPts);
    static OGRGeometry *MakePolygon(const std::vector<Point12da> &aoPts);
    static OGRGeometry *MakeTriangle(const std::array<Point12da, 3> &aoTri);

    GIntBig GetFeatureCount() const;

  public:
    OGR12daLayer(OGR12daDataSource *poDS, const std::string &osLayerName,
                 OGR12daLayerType eType, const std::string &osModelName,
                 const Model12da *poModel);
    ~OGR12daLayer() override;

    void ResetReading() override;
    OGRFeature *GetNextFeature() override;
    GIntBig GetFeatureCount(int bForce) override;
    const OGRFeatureDefn *GetLayerDefn() const override
    {
        return m_poFeatureDefn;
    }
    int TestCapability(const char *) const override
    {
        return FALSE;
    }
};

/************************************************************************/
/*                          OGR12daDataSource                          */
/************************************************************************/

class OGR12daDataSource final : public GDALDataset
{
    std::vector<std::unique_ptr<OGR12daLayer>> m_apoLayers;
    std::map<std::string, Model12da> m_oModels;

    bool Parse(const char *pszText);
    void CreateLayers();

    // Parser helpers
    static bool StartsWithKw(const char *pszLine, const char *pszKw);
    static int CountBraces(const char *pszLine);
    static std::string StripQuotes(const char *pszStr);
    static std::vector<std::string> ParseQuotedStrings(const char *pszLine);
    static bool IsCoordinateLine(const char *pszLine);
    static bool IsNumericishStart(const char *pszLine);
    static bool ParseValue(const char *pszRaw, double &dfVal);
    static std::vector<double> ExtractNumericValues(const char *pszLine,
                                                    int nMax);
    static std::optional<std::string> ParseModelName(const char *pszLine);
    static Point12da MakePoint12da(double dfE, double dfN,
                                   std::optional<double> odfZ,
                                   const std::string &osStyle,
                                   const std::string &osColour);
    static void ComputeSuperAlignment3D(SuperAlignment12da &sa);
    static std::optional<double>
    InterpolateElevation(const std::vector<std::pair<double, double>> &profile,
                         double dfChainage);

  public:
    OGR12daDataSource();

    int GetLayerCount() const override
    {
        return static_cast<int>(m_apoLayers.size());
    }
    const OGRLayer *GetLayer(int i) const override;

    static GDALDataset *Open(GDALOpenInfo *poOpenInfo);
    static int Identify(GDALOpenInfo *poOpenInfo);
};

void RegisterOGR12da();

#endif /* OGR_12DA_H_INCLUDED */
