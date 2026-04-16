/******************************************************************************
 *
 * Project:  OGR 12da Driver
 * Purpose:  Implements OGR12daLayer - feature iteration and schema.
 * Author:   kjarada
 *
 ******************************************************************************
 * Copyright (c) 2026, kjarada
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "ogr_12da.h"
#include "cpl_conv.h"

/************************************************************************/
/*                           OGR12daLayer()                            */
/************************************************************************/

OGR12daLayer::OGR12daLayer(OGR12daDataSource *poDS,
                           const std::string &osLayerName,
                           OGR12daLayerType eType,
                           const std::string &osModelName,
                           const Model12da *poModel)
    : m_poDS(poDS), m_eType(eType), m_osModelName(osModelName),
      m_poModel(poModel)
{
    m_poFeatureDefn = new OGRFeatureDefn(osLayerName.c_str());
    m_poFeatureDefn->Reference();
    SetDescription(osLayerName.c_str());

    switch (eType)
    {
        case OGR12daLayerType::Points:
            BuildPointSchema();
            break;
        case OGR12daLayerType::Lines:
            BuildLineSchema();
            break;
        case OGR12daLayerType::Polygons:
            BuildPolygonSchema();
            break;
        case OGR12daLayerType::TIN:
            BuildTINSchema();
            break;
        case OGR12daLayerType::Trimesh:
            BuildTrimeshSchema();
            break;
        case OGR12daLayerType::Text:
            BuildTextSchema();
            break;
        case OGR12daLayerType::SuperAlignments:
            BuildSuperAlignmentSchema();
            break;
    }
}

/************************************************************************/
/*                          ~OGR12daLayer()                            */
/************************************************************************/

OGR12daLayer::~OGR12daLayer()
{
    if (m_poFeatureDefn)
        m_poFeatureDefn->Release();
}

/************************************************************************/
/*                         Schema builders                             */
/************************************************************************/

void OGR12daLayer::BuildPointSchema()
{
    auto poGeomFieldDefn = std::make_unique<OGRGeomFieldDefn>("", wkbPoint25D);
    m_poFeatureDefn->AddGeomFieldDefn(std::move(poGeomFieldDefn));

    {
        OGRFieldDefn oField("model", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("style", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("colour", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("pid", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
}

void OGR12daLayer::BuildLineSchema()
{
    auto poGeomFieldDefn =
        std::make_unique<OGRGeomFieldDefn>("", wkbLineString25D);
    m_poFeatureDefn->AddGeomFieldDefn(std::move(poGeomFieldDefn));

    {
        OGRFieldDefn oField("model", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("style", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("colour", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("point_count", OFTInteger);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
}

void OGR12daLayer::BuildPolygonSchema()
{
    auto poGeomFieldDefn =
        std::make_unique<OGRGeomFieldDefn>("", wkbPolygon25D);
    m_poFeatureDefn->AddGeomFieldDefn(std::move(poGeomFieldDefn));

    {
        OGRFieldDefn oField("model", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("style", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("colour", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("point_count", OFTInteger);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
}

void OGR12daLayer::BuildTINSchema()
{
    auto poGeomFieldDefn =
        std::make_unique<OGRGeomFieldDefn>("", wkbTriangleZ);
    m_poFeatureDefn->AddGeomFieldDefn(std::move(poGeomFieldDefn));

    {
        OGRFieldDefn oField("model", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
}

void OGR12daLayer::BuildTrimeshSchema()
{
    auto poGeomFieldDefn =
        std::make_unique<OGRGeomFieldDefn>("", wkbTriangleZ);
    m_poFeatureDefn->AddGeomFieldDefn(std::move(poGeomFieldDefn));

    {
        OGRFieldDefn oField("model", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
}

void OGR12daLayer::BuildTextSchema()
{
    auto poGeomFieldDefn = std::make_unique<OGRGeomFieldDefn>("", wkbPoint25D);
    m_poFeatureDefn->AddGeomFieldDefn(std::move(poGeomFieldDefn));

    {
        OGRFieldDefn oField("model", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("label", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("style", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("colour", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("worldsize", OFTReal);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("textstyle", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("angle", OFTReal);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("x_factor", OFTReal);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
}

void OGR12daLayer::BuildSuperAlignmentSchema()
{
    auto poGeomFieldDefn =
        std::make_unique<OGRGeomFieldDefn>("", wkbLineString25D);
    m_poFeatureDefn->AddGeomFieldDefn(std::move(poGeomFieldDefn));

    {
        OGRFieldDefn oField("model", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("name", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("style", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("colour", OFTString);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("chainage", OFTReal);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("h_ip_count", OFTInteger);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
    {
        OGRFieldDefn oField("v_ip_count", OFTInteger);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }
}

/************************************************************************/
/*                        Geometry helpers                             */
/************************************************************************/

/* static */
OGRGeometry *OGR12daLayer::MakePoint(const Point12da &pt)
{
    if (pt.odfZ.has_value())
        return new OGRPoint(pt.dfE, pt.dfN, *pt.odfZ);
    else
        return new OGRPoint(pt.dfE, pt.dfN);
}

/* static */
OGRGeometry *OGR12daLayer::MakeLineString(const std::vector<Point12da> &aoPts)
{
    auto poLine = new OGRLineString();
    for (const auto &pt : aoPts)
    {
        if (pt.odfZ.has_value())
            poLine->addPoint(pt.dfE, pt.dfN, *pt.odfZ);
        else
            poLine->addPoint(pt.dfE, pt.dfN);
    }
    return poLine;
}

/* static */
OGRGeometry *OGR12daLayer::MakePolygon(const std::vector<Point12da> &aoPts)
{
    auto poRing = new OGRLinearRing();
    for (const auto &pt : aoPts)
    {
        if (pt.odfZ.has_value())
            poRing->addPoint(pt.dfE, pt.dfN, *pt.odfZ);
        else
            poRing->addPoint(pt.dfE, pt.dfN);
    }
    poRing->closeRings();
    auto poPoly = new OGRPolygon();
    poPoly->addRingDirectly(poRing);
    return poPoly;
}

/* static */
OGRGeometry *OGR12daLayer::MakeTriangle(const std::array<Point12da, 3> &aoTri)
{
    auto poRing = new OGRLinearRing();
    for (int i = 0; i < 3; i++)
    {
        if (aoTri[i].odfZ.has_value())
            poRing->addPoint(aoTri[i].dfE, aoTri[i].dfN, *aoTri[i].odfZ);
        else
            poRing->addPoint(aoTri[i].dfE, aoTri[i].dfN);
    }
    poRing->closeRings();
    auto poTri = new OGRTriangle();
    poTri->addRingDirectly(poRing);
    return poTri;
}

/************************************************************************/
/*                          Feature count                              */
/************************************************************************/

GIntBig OGR12daLayer::GetFeatureCount() const
{
    if (!m_poModel)
        return 0;

    switch (m_eType)
    {
        case OGR12daLayerType::Points:
            return static_cast<GIntBig>(m_poModel->aoPts.size());
        case OGR12daLayerType::Lines:
            return static_cast<GIntBig>(m_poModel->aoPolylines.size());
        case OGR12daLayerType::Polygons:
            return static_cast<GIntBig>(m_poModel->aoPolygons.size());
        case OGR12daLayerType::TIN:
            return static_cast<GIntBig>(m_poModel->aoTriangles.size());
        case OGR12daLayerType::Trimesh:
            return static_cast<GIntBig>(m_poModel->aoTrimeshes.size());
        case OGR12daLayerType::Text:
            return static_cast<GIntBig>(m_poModel->aoTexts.size());
        case OGR12daLayerType::SuperAlignments:
            return static_cast<GIntBig>(m_poModel->aoSuperAlignments.size());
    }
    return 0;
}

GIntBig OGR12daLayer::GetFeatureCount(int /*bForce*/)
{
    return GetFeatureCount();
}

/************************************************************************/
/*                         ResetReading()                              */
/************************************************************************/

void OGR12daLayer::ResetReading()
{
    m_nNextFID = 0;
    m_bEOF = false;
}

/************************************************************************/
/*                         GetNextFeature()                            */
/************************************************************************/

OGRFeature *OGR12daLayer::GetNextFeature()
{
    if (m_bEOF)
        return nullptr;

    while (true)
    {
        if (m_nNextFID >= GetFeatureCount())
        {
            m_bEOF = true;
            return nullptr;
        }

        OGRFeature *poFeature = nullptr;
        switch (m_eType)
        {
            case OGR12daLayerType::Points:
                poFeature = GetPointFeature(m_nNextFID);
                break;
            case OGR12daLayerType::Lines:
                poFeature = GetLineFeature(m_nNextFID);
                break;
            case OGR12daLayerType::Polygons:
                poFeature = GetPolygonFeature(m_nNextFID);
                break;
            case OGR12daLayerType::TIN:
                poFeature = GetTINFeature(m_nNextFID);
                break;
            case OGR12daLayerType::Trimesh:
                poFeature = GetTrimeshFeature(m_nNextFID);
                break;
            case OGR12daLayerType::Text:
                poFeature = GetTextFeature(m_nNextFID);
                break;
            case OGR12daLayerType::SuperAlignments:
                poFeature = GetSuperAlignmentFeature(m_nNextFID);
                break;
        }

        m_nNextFID++;

        if (poFeature == nullptr)
            continue;

        if ((m_poFilterGeom == nullptr ||
             FilterGeometry(poFeature->GetGeometryRef())) &&
            (m_poAttrQuery == nullptr || m_poAttrQuery->Evaluate(poFeature)))
        {
            return poFeature;
        }

        delete poFeature;
    }
}

/************************************************************************/
/*                       Feature constructors                          */
/************************************************************************/

OGRFeature *OGR12daLayer::GetPointFeature(GIntBig nIdx)
{
    if (!m_poModel || nIdx < 0 ||
        nIdx >= static_cast<GIntBig>(m_poModel->aoPts.size()))
        return nullptr;

    const auto &pt = m_poModel->aoPts[static_cast<size_t>(nIdx)];

    auto poFeature = new OGRFeature(m_poFeatureDefn);
    poFeature->SetFID(nIdx);

    poFeature->SetGeometryDirectly(MakePoint(pt));

    poFeature->SetField("model", m_osModelName.c_str());
    if (!pt.osStyle.empty())
        poFeature->SetField("style", pt.osStyle.c_str());
    if (!pt.osColour.empty())
        poFeature->SetField("colour", pt.osColour.c_str());
    if (!pt.osPid.empty())
        poFeature->SetField("pid", pt.osPid.c_str());

    return poFeature;
}

OGRFeature *OGR12daLayer::GetLineFeature(GIntBig nIdx)
{
    if (!m_poModel || nIdx < 0 ||
        nIdx >= static_cast<GIntBig>(m_poModel->aoPolylines.size()))
        return nullptr;

    const auto &aoPts = m_poModel->aoPolylines[static_cast<size_t>(nIdx)];

    auto poFeature = new OGRFeature(m_poFeatureDefn);
    poFeature->SetFID(nIdx);

    poFeature->SetGeometryDirectly(MakeLineString(aoPts));

    poFeature->SetField("model", m_osModelName.c_str());
    if (!aoPts.empty())
    {
        if (!aoPts[0].osStyle.empty())
            poFeature->SetField("style", aoPts[0].osStyle.c_str());
        if (!aoPts[0].osColour.empty())
            poFeature->SetField("colour", aoPts[0].osColour.c_str());
    }
    poFeature->SetField("point_count", static_cast<int>(aoPts.size()));

    return poFeature;
}

OGRFeature *OGR12daLayer::GetPolygonFeature(GIntBig nIdx)
{
    if (!m_poModel || nIdx < 0 ||
        nIdx >= static_cast<GIntBig>(m_poModel->aoPolygons.size()))
        return nullptr;

    const auto &aoPts = m_poModel->aoPolygons[static_cast<size_t>(nIdx)];

    auto poFeature = new OGRFeature(m_poFeatureDefn);
    poFeature->SetFID(nIdx);

    poFeature->SetGeometryDirectly(MakePolygon(aoPts));

    poFeature->SetField("model", m_osModelName.c_str());
    if (!aoPts.empty())
    {
        if (!aoPts[0].osStyle.empty())
            poFeature->SetField("style", aoPts[0].osStyle.c_str());
        if (!aoPts[0].osColour.empty())
            poFeature->SetField("colour", aoPts[0].osColour.c_str());
    }
    poFeature->SetField("point_count", static_cast<int>(aoPts.size()));

    return poFeature;
}

OGRFeature *OGR12daLayer::GetTINFeature(GIntBig nIdx)
{
    if (!m_poModel || nIdx < 0 ||
        nIdx >= static_cast<GIntBig>(m_poModel->aoTriangles.size()))
        return nullptr;

    const auto &tri = m_poModel->aoTriangles[static_cast<size_t>(nIdx)];

    auto poFeature = new OGRFeature(m_poFeatureDefn);
    poFeature->SetFID(nIdx);

    poFeature->SetGeometryDirectly(MakeTriangle(tri));

    poFeature->SetField("model", m_osModelName.c_str());

    return poFeature;
}

OGRFeature *OGR12daLayer::GetTrimeshFeature(GIntBig nIdx)
{
    if (!m_poModel || nIdx < 0 ||
        nIdx >= static_cast<GIntBig>(m_poModel->aoTrimeshes.size()))
        return nullptr;

    const auto &tri = m_poModel->aoTrimeshes[static_cast<size_t>(nIdx)];

    auto poFeature = new OGRFeature(m_poFeatureDefn);
    poFeature->SetFID(nIdx);

    poFeature->SetGeometryDirectly(MakeTriangle(tri));

    poFeature->SetField("model", m_osModelName.c_str());

    return poFeature;
}

OGRFeature *OGR12daLayer::GetTextFeature(GIntBig nIdx)
{
    if (!m_poModel || nIdx < 0 ||
        nIdx >= static_cast<GIntBig>(m_poModel->aoTexts.size()))
        return nullptr;

    const auto &text = m_poModel->aoTexts[static_cast<size_t>(nIdx)];

    auto poFeature = new OGRFeature(m_poFeatureDefn);
    poFeature->SetFID(nIdx);

    Point12da ptLoc;
    ptLoc.dfE = text.dfE;
    ptLoc.dfN = text.dfN;
    ptLoc.odfZ = text.odfZ;
    poFeature->SetGeometryDirectly(MakePoint(ptLoc));

    poFeature->SetField("model", m_osModelName.c_str());
    poFeature->SetField("label", text.osLabel.c_str());
    if (!text.osStyle.empty())
        poFeature->SetField("style", text.osStyle.c_str());
    if (!text.osColour.empty())
        poFeature->SetField("colour", text.osColour.c_str());
    if (text.odfWorldsize.has_value())
        poFeature->SetField("worldsize", *text.odfWorldsize);
    if (!text.osTextstyle.empty())
        poFeature->SetField("textstyle", text.osTextstyle.c_str());
    if (text.odfAngle.has_value())
        poFeature->SetField("angle", *text.odfAngle);
    if (text.odfXFactor.has_value())
        poFeature->SetField("x_factor", *text.odfXFactor);

    return poFeature;
}

OGRFeature *OGR12daLayer::GetSuperAlignmentFeature(GIntBig nIdx)
{
    if (!m_poModel || nIdx < 0 ||
        nIdx >=
            static_cast<GIntBig>(m_poModel->aoSuperAlignments.size()))
        return nullptr;

    const auto &sa =
        m_poModel->aoSuperAlignments[static_cast<size_t>(nIdx)];

    auto poFeature = new OGRFeature(m_poFeatureDefn);
    poFeature->SetFID(nIdx);

    // Build geometry from 3D coords or horizontal coords
    const auto &aoCoords =
        sa.aoGeometry3D.empty() ? sa.aoHorizontalCoords : sa.aoGeometry3D;
    if (!aoCoords.empty())
    {
        poFeature->SetGeometryDirectly(MakeLineString(aoCoords));
    }

    poFeature->SetField("model", m_osModelName.c_str());
    if (!sa.osName.empty())
        poFeature->SetField("name", sa.osName.c_str());
    if (!sa.osStyle.empty())
        poFeature->SetField("style", sa.osStyle.c_str());
    if (!sa.osColour.empty())
        poFeature->SetField("colour", sa.osColour.c_str());
    if (sa.odfChainage.has_value())
        poFeature->SetField("chainage", *sa.odfChainage);
    poFeature->SetField("h_ip_count",
                         static_cast<int>(sa.aoHorizontalIPs.size()));
    poFeature->SetField("v_ip_count",
                         static_cast<int>(sa.aoVerticalIPs.size()));

    return poFeature;
}
