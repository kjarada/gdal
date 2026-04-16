/******************************************************************************
 *
 * Project:  OGR 12da Driver
 * Purpose:  Implements OGR12daDataSource - 12d Model ASCII file parser.
 * Author:   kjarada
 *
 ******************************************************************************
 * Copyright (c) 2026, kjarada
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "ogr_12da.h"
#include "cpl_conv.h"
#include "cpl_string.h"
#include "cpl_vsi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

/************************************************************************/
/*                        OGR12daDataSource()                          */
/************************************************************************/

OGR12daDataSource::OGR12daDataSource() = default;

/************************************************************************/
/*                            GetLayer()                               */
/************************************************************************/

const OGRLayer *OGR12daDataSource::GetLayer(int i) const
{
    if (i < 0 || i >= static_cast<int>(m_apoLayers.size()))
        return nullptr;
    return m_apoLayers[i].get();
}

/************************************************************************/
/*                         Helper: StartsWithKw                        */
/************************************************************************/

bool OGR12daDataSource::StartsWithKw(const char *pszLine, const char *pszKw)
{
    const size_t nKwLen = strlen(pszKw);
    if (EQUALN(pszLine, pszKw, static_cast<int>(nKwLen)))
    {
        if (pszLine[nKwLen] == '\0' || pszLine[nKwLen] == ' ' ||
            pszLine[nKwLen] == '\t' || pszLine[nKwLen] == '{' ||
            pszLine[nKwLen] == '"')
            return true;
    }
    return false;
}

/************************************************************************/
/*                         Helper: CountBraces                         */
/************************************************************************/

int OGR12daDataSource::CountBraces(const char *pszLine)
{
    int nDelta = 0;
    for (const char *p = pszLine; *p; ++p)
    {
        if (*p == '{')
            nDelta++;
        else if (*p == '}')
            nDelta--;
    }
    return nDelta;
}

/************************************************************************/
/*                         Helper: StripQuotes                         */
/************************************************************************/

std::string OGR12daDataSource::StripQuotes(const char *pszStr)
{
    const size_t nLen = strlen(pszStr);
    if (nLen >= 2 && pszStr[0] == '"' && pszStr[nLen - 1] == '"')
        return std::string(pszStr + 1, nLen - 2);
    return pszStr;
}

/************************************************************************/
/*                     Helper: ParseQuotedStrings                      */
/************************************************************************/

std::vector<std::string>
OGR12daDataSource::ParseQuotedStrings(const char *pszLine)
{
    std::vector<std::string> aoResult;
    const char *p = pszLine;
    while (*p)
    {
        if (*p == '"')
        {
            p++;
            const char *pStart = p;
            while (*p && *p != '"')
                p++;
            aoResult.emplace_back(pStart, p - pStart);
            if (*p == '"')
                p++;
        }
        else
        {
            p++;
        }
    }
    return aoResult;
}

/************************************************************************/
/*                       Helper: ParseValue                            */
/************************************************************************/

bool OGR12daDataSource::ParseValue(const char *pszRaw, double &dfVal)
{
    // Skip whitespace
    while (*pszRaw == ' ' || *pszRaw == '\t')
        pszRaw++;

    if (*pszRaw == '\0')
        return false;

    // Handle hex float (0x...)
    if (pszRaw[0] == '0' && (pszRaw[1] == 'x' || pszRaw[1] == 'X'))
    {
        char *pszEnd = nullptr;
        dfVal = strtod(pszRaw, &pszEnd);
        return pszEnd != pszRaw;
    }

    // Handle regular number (decimal, scientific)
    char *pszEnd = nullptr;
    dfVal = CPLStrtod(pszRaw, &pszEnd);
    if (pszEnd == pszRaw)
        return false;

    // Verify the remainder is whitespace or end
    while (*pszEnd == ' ' || *pszEnd == '\t' || *pszEnd == ',' ||
           *pszEnd == '{' || *pszEnd == '}')
        pszEnd++;

    return true;
}

/************************************************************************/
/*                    Helper: IsCoordinateLine                         */
/************************************************************************/

bool OGR12daDataSource::IsCoordinateLine(const char *pszLine)
{
    // A coordinate line starts with a numeric value (possibly negative)
    // and has 2 or 3 space-separated numeric values
    while (*pszLine == ' ' || *pszLine == '\t')
        pszLine++;

    if (*pszLine == '\0')
        return false;

    // First char must be digit, minus sign, or decimal point
    if (!isdigit(static_cast<unsigned char>(*pszLine)) && *pszLine != '-' &&
        *pszLine != '.')
        return false;

    // Count numeric tokens
    int nCount = 0;
    const char *p = pszLine;
    while (*p)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        double dfVal;
        char *pEnd = nullptr;
        CPLStrtod(p, &pEnd);
        if (pEnd == p)
            return false;
        nCount++;
        p = pEnd;
        if (nCount > 3)
            return false;
    }

    return nCount == 2 || nCount == 3;
}

/************************************************************************/
/*                    Helper: IsNumericishStart                        */
/************************************************************************/

bool OGR12daDataSource::IsNumericishStart(const char *pszLine)
{
    while (*pszLine == ' ' || *pszLine == '\t')
        pszLine++;
    return (isdigit(static_cast<unsigned char>(*pszLine)) || *pszLine == '-' ||
            *pszLine == '.');
}

/************************************************************************/
/*                   Helper: ExtractNumericValues                      */
/************************************************************************/

std::vector<double> OGR12daDataSource::ExtractNumericValues(const char *pszLine,
                                                            int nMax)
{
    std::vector<double> aoResult;
    const char *p = pszLine;
    while (*p && static_cast<int>(aoResult.size()) < nMax)
    {
        while (*p && !isdigit(static_cast<unsigned char>(*p)) && *p != '-' &&
               *p != '.')
            p++;
        if (*p == '\0')
            break;
        char *pEnd = nullptr;
        double dfVal = CPLStrtod(p, &pEnd);
        if (pEnd == p)
        {
            p++;
            continue;
        }
        aoResult.push_back(dfVal);
        p = pEnd;
    }
    return aoResult;
}

/************************************************************************/
/*                      Helper: ParseModelName                         */
/************************************************************************/

std::optional<std::string>
OGR12daDataSource::ParseModelName(const char *pszLine)
{
    if (!StartsWithKw(pszLine, "model"))
        return std::nullopt;

    auto aoQuoted = ParseQuotedStrings(pszLine);
    if (!aoQuoted.empty())
        return aoQuoted[0];

    // Fallback: unquoted model name
    const char *p = pszLine + 5;  // skip "model"
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '\0' || *p == '{' || *p == '}')
        return std::nullopt;

    const char *pStart = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '{' && *p != '}')
        p++;
    return std::string(pStart, p - pStart);
}

/************************************************************************/
/*                       Helper: MakePoint12da                         */
/************************************************************************/

Point12da OGR12daDataSource::MakePoint12da(double dfE, double dfN,
                                           std::optional<double> odfZ,
                                           const std::string &osStyle,
                                           const std::string &osColour)
{
    // Treat -999 as null Z (standard 12d sentinel)
    if (odfZ.has_value() && odfZ.value() <= -999.0)
        odfZ.reset();

    Point12da pt;
    pt.dfE = dfE;
    pt.dfN = dfN;
    pt.odfZ = odfZ;
    pt.osStyle = osStyle;
    pt.osColour = osColour;
    return pt;
}

/************************************************************************/
/*                   Helper: InterpolateElevation                      */
/************************************************************************/

std::optional<double> OGR12daDataSource::InterpolateElevation(
    const std::vector<std::pair<double, double>> &profile, double dfChainage)
{
    if (profile.empty())
        return std::nullopt;

    if (dfChainage <= profile.front().first)
        return profile.front().second;
    if (dfChainage >= profile.back().first)
        return profile.back().second;

    for (size_t i = 1; i < profile.size(); i++)
    {
        if (dfChainage <= profile[i].first)
        {
            const double dfT = (dfChainage - profile[i - 1].first) /
                               (profile[i].first - profile[i - 1].first);
            return profile[i - 1].second +
                   dfT * (profile[i].second - profile[i - 1].second);
        }
    }
    return profile.back().second;
}

/************************************************************************/
/*                   Helper: ComputeSuperAlignment3D                   */
/************************************************************************/

void OGR12daDataSource::ComputeSuperAlignment3D(SuperAlignment12da &sa)
{
    // Determine horizontal coordinates
    std::vector<Point12da> aoHCoords;

    if (!sa.aoHorizontalCoords.empty())
    {
        aoHCoords = sa.aoHorizontalCoords;
    }
    else if (!sa.aoHorizontalIPs.empty())
    {
        // Build approximate geometry from IPs
        for (const auto &ip : sa.aoHorizontalIPs)
        {
            aoHCoords.push_back(
                MakePoint12da(ip.dfX, ip.dfY, std::nullopt, "", ""));
        }
    }
    else
    {
        return;
    }

    if (aoHCoords.empty())
        return;

    // Build vertical profile from vertical coords
    const auto &aoVProfile = sa.aoVerticalCoords;
    const bool bHasVertical = !aoVProfile.empty();

    // Calculate cumulative chainage for horizontal coords
    const double dfBaseChainage = sa.odfChainage.value_or(0.0);
    std::vector<double> aoChainages;
    aoChainages.reserve(aoHCoords.size());
    double dfCumulDist = 0.0;
    aoChainages.push_back(dfBaseChainage);

    for (size_t i = 1; i < aoHCoords.size(); i++)
    {
        const double dfDx = aoHCoords[i].dfE - aoHCoords[i - 1].dfE;
        const double dfDy = aoHCoords[i].dfN - aoHCoords[i - 1].dfN;
        dfCumulDist += std::sqrt(dfDx * dfDx + dfDy * dfDy);
        aoChainages.push_back(dfBaseChainage + dfCumulDist);
    }

    // Create 3D points
    sa.aoGeometry3D.clear();
    sa.aoGeometry3D.reserve(aoHCoords.size());

    for (size_t i = 0; i < aoHCoords.size(); i++)
    {
        std::optional<double> odfZ;
        if (bHasVertical)
        {
            odfZ = InterpolateElevation(aoVProfile, aoChainages[i]);
        }
        else
        {
            odfZ = aoHCoords[i].odfZ;
        }

        Point12da pt;
        pt.dfE = aoHCoords[i].dfE;
        pt.dfN = aoHCoords[i].dfN;
        pt.odfZ = odfZ;
        pt.osStyle = sa.osStyle;
        pt.osColour = sa.osColour;
        sa.aoGeometry3D.push_back(std::move(pt));
    }
}

/************************************************************************/
/*                              Parse()                                */
/************************************************************************/

bool OGR12daDataSource::Parse(const char *pszText)
{
    std::string osCurModel = "Default";
    m_oModels[osCurModel] = Model12da();

    // String state
    bool bInString = false;
    int nStringBr = 0;
    bool bInData = false;
    int nDataBr = 0;
    bool bInPointData = false;
    int nPdBr = 0;
    std::vector<Point12da> aoCurString;
    bool bCurClosed = false;
    std::vector<std::string> aoCurPointIds;
    std::vector<Attr12da> aoCurStringAttrs;
    bool bCurIsBreaklinePoint = false;
    std::optional<double> odfCurStringZ;

    // Text string state
    bool bInTextString = false;
    std::optional<double> odfTextX, odfTextY, odfTextZ;
    std::string osTextLabel;
    std::optional<double> odfTextWorldsize;
    std::string osTextstyle;
    std::optional<double> odfTextAngle;
    std::optional<double> odfTextXFactor;

    // Super alignment state
    bool bInSuperAlignment = false;
    int nSaDataDepth = 0;
    std::optional<SuperAlignment12da> oSaCurrent;
    bool bInHorizontalParts = false;
    bool bInVerticalParts = false;
    bool bInHorizontalData = false;
    bool bInVerticalData = false;
    bool bInIpBlock = false;
    bool bInArcBlock = false;
    std::optional<double> odfIpX, odfIpY;
    std::optional<double> odfArcRadius;
    int nPartsBracketDepth = 0;
    bool bInGeometryData = false;
    int nGeometryDataBr = 0;
    bool bInGeometryArc = false;
    bool bInGeometrySpiral = false;
    std::optional<double> odfGeomRadius;
    bool bGeomMajor = false;
    double dfSpiralL1 = 0, dfSpiralL2 = 0, dfSpiralR1 = 0, dfSpiralR2 = 0;
    std::vector<GeometrySegment12da> aoHGeometrySegments;
    std::vector<GeometrySegment12da> aoVGeometrySegments;

    // Global style
    std::string osCurStyle;
    std::string osCurColour;

    // Attribute parsing state
    bool bInAttributes = false;
    int nAttrBr = 0;
    enum class AttrTarget
    {
        String,
        Tin,
        Model
    };
    std::optional<AttrTarget> oeAttrTarget;
    std::string osAttrTargetModel;
    std::vector<std::vector<Attr12da>> aoAttrListStack;
    std::vector<std::string> aoAttrGroupNameStack;

    // TIN state
    bool bInTin = false;
    int nTinBr = 0;
    bool bTinHasOpen = false;
    std::vector<Point12da> aoTinVerts;
    std::vector<std::array<int, 3>> aoTinFaces;
    bool bInTinVerts = false;
    int nTvBr = 0;
    bool bInTinFaces = false;
    int nTfBr = 0;
    bool bInTinNulling = false;
    int nTnBr = 0;
    std::vector<int> aoTinNulling;
    std::optional<std::string> osTinName;
    double dfTinNullLength = 0.0;
    std::vector<Attr12da> aoTinAttrs;

    // Trimesh state
    bool bInPrim3d = false;
    int nPrimBr = 0;
    bool bInTrimesh3d = false;
    bool bInTriVerts = false;
    int nVBr = 0;
    std::vector<Point12da> aoTriVerts;
    bool bInTriFaces = false;
    int nFBr = 0;
    std::vector<std::array<int, 3>> aoTriFaces;

    // Transformation
    bool bInTransformation = false;
    int nTransBr = 0;
    std::vector<double> aoCurMatrix;

    // Extrude block state
    bool bInExtrudeValue = false;
    int nExtrudeBr = 0;

    // Line iteration
    const char *pCur = pszText;
    while (*pCur)
    {
        // Find line end
        const char *pLineStart = pCur;
        while (*pCur && *pCur != '\n' && *pCur != '\r')
            pCur++;

        std::string osRaw(pLineStart, pCur - pLineStart);

        // Skip newlines
        if (*pCur == '\r')
            pCur++;
        if (*pCur == '\n')
            pCur++;

        // Trim
        const char *pszLine = osRaw.c_str();
        while (*pszLine == ' ' || *pszLine == '\t')
            pszLine++;
        size_t nLineLen = strlen(pszLine);
        while (nLineLen > 0 &&
               (pszLine[nLineLen - 1] == ' ' || pszLine[nLineLen - 1] == '\t'))
            nLineLen--;
        std::string osLine(pszLine, nLineLen);
        pszLine = osLine.c_str();

        if (osLine.empty() || osLine.substr(0, 2) == "//")
            continue;

        // Compute brace delta
        const int nDelta = CountBraces(osRaw.c_str());

        // --- Model detection ---
        auto osNewModel = ParseModelName(pszLine);
        if (osNewModel.has_value())
        {
            // Flush any open string
            if (bInString && !aoCurString.empty())
            {
                auto &oDest = m_oModels[osCurModel];
                if (bCurIsBreaklinePoint)
                {
                    for (auto &pt : aoCurString)
                    {
                        oDest.aoPts.push_back(pt);
                    }
                }
                else if (bCurClosed && aoCurString.size() >= 3)
                {
                    oDest.aoPolygons.push_back(aoCurString);
                }
                else if (aoCurString.size() == 1)
                {
                    oDest.aoPts.push_back(aoCurString[0]);
                }
                else if (aoCurString.size() >= 2)
                {
                    oDest.aoPolylines.push_back(aoCurString);
                }
            }

            bInString = false;
            nStringBr = 0;
            bInData = false;
            nDataBr = 0;
            bInPointData = false;
            nPdBr = 0;
            aoCurString.clear();
            bCurClosed = false;
            aoCurPointIds.clear();
            aoCurStringAttrs.clear();
            bCurIsBreaklinePoint = false;

            osCurModel = *osNewModel;
            if (m_oModels.find(osCurModel) == m_oModels.end())
                m_oModels[osCurModel] = Model12da();
            osCurStyle.clear();
            osCurColour.clear();
        }

        // --- Attribute parsing ---
        const bool bAttrOpen =
            StartsWithKw(pszLine, "attributes") && osLine.find('{') != std::string::npos;
        if (bAttrOpen)
        {
            if (!bInAttributes)
            {
                bInAttributes = true;
                nAttrBr = 0;
                aoAttrListStack.clear();
                aoAttrGroupNameStack.clear();
                aoAttrListStack.push_back({});

                if (bInString && !bInData)
                {
                    oeAttrTarget = AttrTarget::String;
                }
                else if (bInTin)
                {
                    oeAttrTarget = AttrTarget::Tin;
                }
                else
                {
                    oeAttrTarget = AttrTarget::Model;
                    osAttrTargetModel = osCurModel;
                }
            }
        }

        if (bInAttributes)
        {
            // group "name" {
            if (StartsWithKw(pszLine, "group"))
            {
                auto aoQs = ParseQuotedStrings(pszLine);
                std::string osGroupName =
                    aoQs.empty() ? "unnamed" : aoQs[0];
                aoAttrGroupNameStack.push_back(osGroupName);
                aoAttrListStack.push_back({});
            }

            // text "key" "value"
            if (StartsWithKw(pszLine, "text"))
            {
                auto aoQs = ParseQuotedStrings(pszLine);
                if (aoQs.size() >= 2 && !aoAttrListStack.empty())
                {
                    Attr12da attr;
                    attr.eType = Attr12da::Type::Text;
                    attr.osName = aoQs[0];
                    attr.osTextValue = aoQs[1];
                    aoAttrListStack.back().push_back(std::move(attr));
                }
            }

            // real "key" value
            if (StartsWithKw(pszLine, "real"))
            {
                const char *pRest = pszLine + 4;
                while (*pRest == ' ' || *pRest == '\t')
                    pRest++;
                auto aoQs = ParseQuotedStrings(pRest);
                if (!aoQs.empty())
                {
                    // Find the value after the quoted name
                    const char *pVal = strstr(pRest, aoQs[0].c_str());
                    if (pVal)
                    {
                        pVal += aoQs[0].size() + 1;  // skip closing quote
                        double dfVal = 0.0;
                        if (ParseValue(pVal, dfVal) && !aoAttrListStack.empty())
                        {
                            Attr12da attr;
                            attr.eType = Attr12da::Type::Real;
                            attr.osName = aoQs[0];
                            attr.dfRealValue = dfVal;
                            aoAttrListStack.back().push_back(std::move(attr));
                        }
                    }
                }
            }

            // integer "key" value
            if (StartsWithKw(pszLine, "integer"))
            {
                const char *pRest = pszLine + 7;
                while (*pRest == ' ' || *pRest == '\t')
                    pRest++;
                auto aoQs = ParseQuotedStrings(pRest);
                if (!aoQs.empty())
                {
                    const char *pVal = strstr(pRest, aoQs[0].c_str());
                    if (pVal)
                    {
                        pVal += aoQs[0].size() + 1;
                        double dfVal = 0.0;
                        if (ParseValue(pVal, dfVal) && !aoAttrListStack.empty())
                        {
                            Attr12da attr;
                            attr.eType = Attr12da::Type::Integer;
                            attr.osName = aoQs[0];
                            attr.nIntValue = static_cast<int64_t>(dfVal);
                            aoAttrListStack.back().push_back(std::move(attr));
                        }
                    }
                }
            }

            // Track brace depth for attributes
            const int nOpenCount = static_cast<int>(
                std::count(osRaw.begin(), osRaw.end(), '{'));
            const int nCloseCount = static_cast<int>(
                std::count(osRaw.begin(), osRaw.end(), '}'));
            nAttrBr += nOpenCount - nCloseCount;

            // Close groups
            for (int g = 0; g < nCloseCount; g++)
            {
                if (aoAttrListStack.size() > 1 &&
                    !aoAttrGroupNameStack.empty())
                {
                    auto aoChildren = std::move(aoAttrListStack.back());
                    aoAttrListStack.pop_back();
                    std::string osGroupName = aoAttrGroupNameStack.back();
                    aoAttrGroupNameStack.pop_back();

                    Attr12da groupAttr;
                    groupAttr.eType = Attr12da::Type::Group;
                    groupAttr.osName = std::move(osGroupName);
                    groupAttr.aoChildren = std::move(aoChildren);
                    if (!aoAttrListStack.empty())
                        aoAttrListStack.back().push_back(std::move(groupAttr));
                }
            }

            if (nAttrBr <= 0)
            {
                std::vector<Attr12da> aoCommitted;
                if (!aoAttrListStack.empty())
                {
                    aoCommitted = std::move(aoAttrListStack.back());
                    aoAttrListStack.clear();
                }
                aoAttrGroupNameStack.clear();

                if (oeAttrTarget.has_value())
                {
                    switch (*oeAttrTarget)
                    {
                        case AttrTarget::String:
                            aoCurStringAttrs.insert(aoCurStringAttrs.end(),
                                                    aoCommitted.begin(),
                                                    aoCommitted.end());
                            break;
                        case AttrTarget::Tin:
                            aoTinAttrs.insert(aoTinAttrs.end(),
                                              aoCommitted.begin(),
                                              aoCommitted.end());
                            break;
                        case AttrTarget::Model:
                        {
                            auto &oDest = m_oModels[osAttrTargetModel];
                            oDest.aoAttributes.insert(
                                oDest.aoAttributes.end(), aoCommitted.begin(),
                                aoCommitted.end());
                            break;
                        }
                    }
                    oeAttrTarget.reset();
                }

                bInAttributes = false;
                nAttrBr = 0;
            }
            // Don't continue - some lines combine attribute close with other keywords
        }

        // --- Style / colour / closed ---
        if (bInString)
        {
            std::string osLower = osLine;
            for (auto &c : osLower)
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

            if ((osLower.find("closed") != std::string::npos &&
                 (osLower.find("true") != std::string::npos ||
                  osLower.find("1") != std::string::npos)) ||
                (osLower.find("end_style") != std::string::npos &&
                 osLower.find("closed") != std::string::npos))
            {
                bCurClosed = true;
            }
        }

        if (StartsWithKw(pszLine, "style"))
        {
            auto aoQs = ParseQuotedStrings(pszLine);
            if (!aoQs.empty())
            {
                if (bInString)
                    osCurStyle = aoQs[0];
                else
                    osCurStyle = aoQs[0];
            }
        }

        if (StartsWithKw(pszLine, "colour"))
        {
            auto aoQs = ParseQuotedStrings(pszLine);
            if (!aoQs.empty())
            {
                osCurColour = aoQs[0];
            }
            else
            {
                // Unquoted colour
                const char *pR = pszLine + 6;
                while (*pR == ' ' || *pR == '\t')
                    pR++;
                if (*pR)
                    osCurColour = pR;
            }
        }

        // --- Extrude block ---
        if (StartsWithKw(pszLine, "extrude_value") &&
            osLine.find('{') != std::string::npos)
        {
            bInExtrudeValue = true;
            nExtrudeBr = 0;
        }
        if (bInExtrudeValue)
        {
            nExtrudeBr += nDelta;
            if (nExtrudeBr <= 0)
            {
                bInExtrudeValue = false;
                nExtrudeBr = 0;
            }
        }

        // --- Breakline point ---
        {
            std::string osLower = osLine;
            for (auto &c : osLower)
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (osLower.find("breakline point") != std::string::npos)
            {
                bCurIsBreaklinePoint = true;
            }
        }

        // --- Start string ---
        if (!bInString && !bInExtrudeValue &&
            (StartsWithKw(pszLine, "string") ||
             StartsWithKw(pszLine, "super string")) &&
            osLine.find('{') != std::string::npos)
        {
            std::string osLower = osLine;
            for (auto &c : osLower)
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

            // Super alignment?
            if (osLower.find("super_alignment") != std::string::npos)
            {
                bInSuperAlignment = true;
                bInString = true;
                nStringBr = nDelta;
                nSaDataDepth = 0;
                oSaCurrent = SuperAlignment12da();
                bInHorizontalParts = false;
                bInVerticalParts = false;
                bInHorizontalData = false;
                bInVerticalData = false;
                bInIpBlock = false;
                bInArcBlock = false;
                nPartsBracketDepth = 0;
                continue;
            }

            bInString = true;
            nStringBr = 0;
            bInData = false;
            nDataBr = 0;
            bInPointData = false;
            nPdBr = 0;
            aoCurString.clear();
            bCurClosed = false;
            aoCurPointIds.clear();
            aoCurStringAttrs.clear();
            bCurIsBreaklinePoint = false;
            odfCurStringZ.reset();
            osCurStyle.clear();
            osCurColour.clear();

            // Text string?
            bInTextString = (osLower.find("string text") != std::string::npos ||
                             osLower.find("string super text") != std::string::npos);
            if (bInTextString)
            {
                odfTextX.reset();
                odfTextY.reset();
                odfTextZ.reset();
                osTextLabel.clear();
                odfTextWorldsize.reset();
                osTextstyle.clear();
                odfTextAngle.reset();
                odfTextXFactor.reset();
            }
        }

        // --- Super alignment parsing ---
        if (bInSuperAlignment && bInString)
        {
            std::string osLower = osLine;
            for (auto &c : osLower)
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

            // Name
            if (StartsWithKw(pszLine, "name") && oSaCurrent.has_value() &&
                oSaCurrent->osName.empty())
            {
                auto aoQs = ParseQuotedStrings(pszLine);
                if (!aoQs.empty())
                    oSaCurrent->osName = aoQs[0];
            }

            // Chainage
            if (StartsWithKw(pszLine, "chainage") && nSaDataDepth == 0 &&
                !bInGeometryData && oSaCurrent.has_value() &&
                !oSaCurrent->odfChainage.has_value())
            {
                auto aoVals = ExtractNumericValues(pszLine, 2);
                if (aoVals.size() >= 2)
                    oSaCurrent->odfChainage = aoVals[1];
            }

            // Colour
            if (StartsWithKw(pszLine, "colour") && oSaCurrent.has_value() &&
                oSaCurrent->osColour.empty())
            {
                auto aoQs = ParseQuotedStrings(pszLine);
                if (!aoQs.empty())
                    oSaCurrent->osColour = aoQs[0];
            }

            // Style
            if (StartsWithKw(pszLine, "style") && oSaCurrent.has_value() &&
                oSaCurrent->osStyle.empty())
            {
                auto aoQs = ParseQuotedStrings(pszLine);
                if (!aoQs.empty())
                    oSaCurrent->osStyle = aoQs[0];
            }

            // Horizontal parts
            if (osLower.find("horizontal_parts") != std::string::npos &&
                osLine.find('{') != std::string::npos)
            {
                bInHorizontalParts = true;
                nPartsBracketDepth = 0;
            }
            // Vertical parts
            if (osLower.find("vertical_parts") != std::string::npos &&
                osLine.find('{') != std::string::npos)
            {
                bInVerticalParts = true;
                nPartsBracketDepth = 0;
            }
            // Horizontal data
            if (osLower.find("horizontal_data") != std::string::npos &&
                osLine.find('{') != std::string::npos)
            {
                bInHorizontalData = true;
            }
            // Vertical data
            if (osLower.find("vertical_data") != std::string::npos &&
                osLine.find('{') != std::string::npos)
            {
                bInVerticalData = true;
            }

            // IP blocks in horizontal/vertical parts
            if ((bInHorizontalParts || bInVerticalParts) &&
                StartsWithKw(pszLine, "ip") &&
                osLine.find('{') != std::string::npos)
            {
                bInIpBlock = true;
                odfIpX.reset();
                odfIpY.reset();
                odfArcRadius.reset();
            }

            if ((bInHorizontalParts || bInVerticalParts) &&
                StartsWithKw(pszLine, "arc") &&
                osLine.find('{') != std::string::npos)
            {
                bInArcBlock = true;
                odfIpX.reset();
                odfIpY.reset();
                odfArcRadius.reset();
            }

            // Parse x, y, r inside IP/arc blocks
            if ((bInIpBlock || bInArcBlock) &&
                StartsWithKw(pszLine, "x") &&
                osLower.find("x_factor") == std::string::npos)
            {
                auto aoVals = ExtractNumericValues(pszLine, 2);
                if (aoVals.size() >= 2)
                    odfIpX = aoVals[1];
            }
            if ((bInIpBlock || bInArcBlock) && StartsWithKw(pszLine, "y"))
            {
                auto aoVals = ExtractNumericValues(pszLine, 2);
                if (aoVals.size() >= 2)
                    odfIpY = aoVals[1];
            }
            if (bInArcBlock && StartsWithKw(pszLine, "r"))
            {
                auto aoVals = ExtractNumericValues(pszLine, 2);
                if (aoVals.size() >= 2)
                    odfArcRadius = aoVals[1];
            }

            // End of IP/arc block
            if ((bInIpBlock || bInArcBlock) &&
                osLine.find('}') != std::string::npos &&
                osLine.find('{') == std::string::npos)
            {
                if (odfIpX.has_value() && odfIpY.has_value())
                {
                    if (bInHorizontalParts && oSaCurrent.has_value())
                    {
                        HorizontalIP12da hip;
                        hip.dfX = *odfIpX;
                        hip.dfY = *odfIpY;
                        hip.odfRadius =
                            bInArcBlock ? odfArcRadius : std::nullopt;
                        oSaCurrent->aoHorizontalIPs.push_back(hip);
                    }
                    else if (bInVerticalParts && oSaCurrent.has_value())
                    {
                        VerticalIP12da vip;
                        vip.dfChainage = *odfIpX;
                        vip.dfElevation = *odfIpY;
                        oSaCurrent->aoVerticalIPs.push_back(vip);
                    }
                }
                bInIpBlock = false;
                bInArcBlock = false;
            }

            // Geometry data blocks
            if (osLower.find("geometry_data") != std::string::npos &&
                osLine.find('{') != std::string::npos)
            {
                bInGeometryData = true;
                nGeometryDataBr = 0;
            }

            if (bInGeometryData)
            {
                // Arc block in geometry_data
                if (StartsWithKw(pszLine, "arc") &&
                    osLine.find('{') != std::string::npos)
                {
                    bInGeometryArc = true;
                    odfGeomRadius.reset();
                    bGeomMajor = false;
                }
                // Spiral block
                if (StartsWithKw(pszLine, "spiral") &&
                    osLine.find('{') != std::string::npos)
                {
                    bInGeometrySpiral = true;
                    dfSpiralL1 = dfSpiralL2 = dfSpiralR1 = dfSpiralR2 = 0;
                }

                if (bInGeometryArc)
                {
                    if (StartsWithKw(pszLine, "radius"))
                    {
                        auto aoVals = ExtractNumericValues(pszLine, 2);
                        if (aoVals.size() >= 2)
                            odfGeomRadius = aoVals[1];
                    }
                    if (osLower.find("major") != std::string::npos)
                        bGeomMajor = true;

                    if (osLine.find('}') != std::string::npos &&
                        osLine.find('{') == std::string::npos)
                    {
                        GeometrySegment12da seg;
                        seg.eType = GeometrySegment12da::Type::Arc;
                        seg.dfRadius = odfGeomRadius.value_or(0.0);
                        seg.bMajor = bGeomMajor;
                        if (bInHorizontalData)
                            aoHGeometrySegments.push_back(seg);
                        else
                            aoVGeometrySegments.push_back(seg);
                        bInGeometryArc = false;
                    }
                }

                if (bInGeometrySpiral)
                {
                    if (StartsWithKw(pszLine, "l1"))
                    {
                        auto v = ExtractNumericValues(pszLine, 2);
                        if (v.size() >= 2)
                            dfSpiralL1 = v[1];
                    }
                    if (StartsWithKw(pszLine, "l2"))
                    {
                        auto v = ExtractNumericValues(pszLine, 2);
                        if (v.size() >= 2)
                            dfSpiralL2 = v[1];
                    }
                    if (StartsWithKw(pszLine, "r1"))
                    {
                        auto v = ExtractNumericValues(pszLine, 2);
                        if (v.size() >= 2)
                            dfSpiralR1 = v[1];
                    }
                    if (StartsWithKw(pszLine, "r2"))
                    {
                        auto v = ExtractNumericValues(pszLine, 2);
                        if (v.size() >= 2)
                            dfSpiralR2 = v[1];
                    }
                    if (osLine.find('}') != std::string::npos &&
                        osLine.find('{') == std::string::npos)
                    {
                        GeometrySegment12da seg;
                        seg.eType = GeometrySegment12da::Type::Spiral;
                        seg.dfL1 = dfSpiralL1;
                        seg.dfL2 = dfSpiralL2;
                        seg.dfR1 = dfSpiralR1;
                        seg.dfR2 = dfSpiralR2;
                        if (bInHorizontalData)
                            aoHGeometrySegments.push_back(seg);
                        else
                            aoVGeometrySegments.push_back(seg);
                        bInGeometrySpiral = false;
                    }
                }

                // Straight
                if (StartsWithKw(pszLine, "straight"))
                {
                    GeometrySegment12da seg;
                    seg.eType = GeometrySegment12da::Type::Straight;
                    if (bInHorizontalData)
                        aoHGeometrySegments.push_back(seg);
                    else
                        aoVGeometrySegments.push_back(seg);
                }

                // Parabola
                if (StartsWithKw(pszLine, "parabola"))
                {
                    // TODO: parse parabola params from sub-block
                    GeometrySegment12da seg;
                    seg.eType = GeometrySegment12da::Type::Parabola;
                    aoVGeometrySegments.push_back(seg);
                }

                nGeometryDataBr += nDelta;
                if (nGeometryDataBr <= 0)
                {
                    bInGeometryData = false;
                    nGeometryDataBr = 0;
                }
            }

            // Data blocks inside horizontal/vertical data
            if ((bInHorizontalData || bInVerticalData) && !bInGeometryData)
            {
                if ((StartsWithKw(pszLine, "data") ||
                     StartsWithKw(pszLine, "data_2d") ||
                     StartsWithKw(pszLine, "data_3d")) &&
                    osLine.find('{') != std::string::npos)
                {
                    nSaDataDepth++;
                }

                // Coordinate lines
                if (nSaDataDepth > 0 && IsCoordinateLine(pszLine))
                {
                    auto aoVals = ExtractNumericValues(pszLine, 3);
                    if (aoVals.size() >= 2 && oSaCurrent.has_value())
                    {
                        if (bInHorizontalData)
                        {
                            double dfE = aoVals[0], dfN = aoVals[1];
                            std::optional<double> odfZ;
                            if (aoVals.size() >= 3)
                                odfZ = aoVals[2];
                            oSaCurrent->aoHorizontalCoords.push_back(
                                MakePoint12da(dfE, dfN, odfZ, "", ""));
                        }
                        else if (bInVerticalData)
                        {
                            oSaCurrent->aoVerticalCoords.emplace_back(
                                aoVals[0], aoVals[1]);
                        }
                    }
                }

                if (nSaDataDepth > 0 &&
                    osLine.find('}') != std::string::npos &&
                    osLine.find('{') == std::string::npos && !bInGeometryData)
                {
                    nSaDataDepth--;
                }
            }

            // Track bracket depth for super_alignment
            nStringBr += nDelta;
            if (nStringBr <= 0)
            {
                // Finalize super alignment
                if (oSaCurrent.has_value())
                {
                    oSaCurrent->aoHorizontalGeometry =
                        std::move(aoHGeometrySegments);
                    oSaCurrent->aoVerticalGeometry =
                        std::move(aoVGeometrySegments);
                    ComputeSuperAlignment3D(*oSaCurrent);

                    if (!oSaCurrent->aoHorizontalIPs.empty() ||
                        !oSaCurrent->aoHorizontalCoords.empty() ||
                        !oSaCurrent->aoGeometry3D.empty())
                    {
                        m_oModels[osCurModel].aoSuperAlignments.push_back(
                            std::move(*oSaCurrent));
                    }
                    oSaCurrent.reset();
                }

                bInSuperAlignment = false;
                bInString = false;
                nStringBr = 0;
                nSaDataDepth = 0;
                bInHorizontalParts = false;
                bInVerticalParts = false;
                bInHorizontalData = false;
                bInVerticalData = false;
                nPartsBracketDepth = 0;
                aoHGeometrySegments.clear();
                aoVGeometrySegments.clear();
            }
            continue;
        }

        // --- Text string block properties ---
        if (bInString && bInTextString)
        {
            std::string osLower = osLine;
            for (auto &c : osLower)
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

            if (StartsWithKw(pszLine, "x") &&
                osLower.find("x_factor") == std::string::npos)
            {
                auto aoVals = ExtractNumericValues(pszLine, 2);
                if (aoVals.size() >= 2)
                    odfTextX = aoVals[1];
            }
            if (StartsWithKw(pszLine, "y"))
            {
                auto aoVals = ExtractNumericValues(pszLine, 2);
                if (aoVals.size() >= 2)
                    odfTextY = aoVals[1];
            }
            if (StartsWithKw(pszLine, "z"))
            {
                auto aoQs = ParseQuotedStrings(pszLine);
                std::string osLowerZ = osLine;
                for (auto &c : osLowerZ)
                    c = static_cast<char>(
                        tolower(static_cast<unsigned char>(c)));
                if (osLowerZ.find("null") == std::string::npos)
                {
                    auto aoVals = ExtractNumericValues(pszLine, 2);
                    if (aoVals.size() >= 2)
                        odfTextZ = aoVals[1];
                }
            }
            if (StartsWithKw(pszLine, "text"))
            {
                auto aoQs = ParseQuotedStrings(pszLine);
                if (!aoQs.empty())
                    osTextLabel = aoQs[0];
            }
            if (StartsWithKw(pszLine, "worldsize"))
            {
                auto aoVals = ExtractNumericValues(pszLine, 2);
                if (aoVals.size() >= 2)
                    odfTextWorldsize = aoVals[1];
            }
            if (StartsWithKw(pszLine, "textstyle"))
            {
                auto aoQs = ParseQuotedStrings(pszLine);
                if (!aoQs.empty())
                    osTextstyle = aoQs[0];
            }
            if (StartsWithKw(pszLine, "angle"))
            {
                auto aoVals = ExtractNumericValues(pszLine, 2);
                if (aoVals.size() >= 2)
                    odfTextAngle = aoVals[1];
            }
            if (StartsWithKw(pszLine, "x_factor"))
            {
                auto aoVals = ExtractNumericValues(pszLine, 2);
                if (aoVals.size() >= 2)
                    odfTextXFactor = aoVals[1];
            }
        }

        // --- String-level z value ---
        if (bInString && !bInTextString && !bInData && !bInExtrudeValue)
        {
            if (StartsWithKw(pszLine, "z"))
            {
                std::string osLower = osLine;
                for (auto &c : osLower)
                    c = static_cast<char>(
                        tolower(static_cast<unsigned char>(c)));
                if (osLower.find("null") == std::string::npos)
                {
                    auto aoVals = ExtractNumericValues(pszLine, 2);
                    if (aoVals.size() >= 2)
                        odfCurStringZ = aoVals[1];
                }
            }
        }

        // --- Data block start ---
        if (bInString && !bInData && !bInExtrudeValue)
        {
            std::string osLower = osLine;
            for (auto &c : osLower)
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            bool bIsDataBlock =
                (osLower.find("data") == 0 || osLower.find("geometry") == 0) &&
                osLine.find('{') != std::string::npos;
            if (bIsDataBlock)
            {
                bInData = true;
                nDataBr = 0;
            }
        }

        // --- point_data block ---
        if (bInString && !bInPointData && !bInExtrudeValue)
        {
            if (StartsWithKw(pszLine, "point_data") &&
                osLine.find('{') != std::string::npos)
            {
                bInPointData = true;
                nPdBr = 0;
            }
        }

        // Collect point ids
        if (bInString && bInPointData && !bInExtrudeValue &&
            osLine.find('"') != std::string::npos)
        {
            auto aoIds = ParseQuotedStrings(pszLine);
            aoCurPointIds.insert(aoCurPointIds.end(), aoIds.begin(),
                                 aoIds.end());
        }

        // --- Name inside string ---
        if (bInString && StartsWithKw(pszLine, "name"))
        {
            // We store names on the first point's pid if needed
        }

        // --- Text entities (inline: text "label" E N [Z]) ---
        if (!bInExtrudeValue && !bInString && StartsWithKw(pszLine, "text"))
        {
            auto aoQs = ParseQuotedStrings(pszLine);
            if (!aoQs.empty())
            {
                auto aoNums = ExtractNumericValues(pszLine, 3);
                if (aoNums.size() >= 2)
                {
                    TextEnt12da text;
                    text.osLabel = aoQs[0];
                    text.dfE = aoNums[0];
                    text.dfN = aoNums[1];
                    if (aoNums.size() >= 3)
                        text.odfZ = aoNums[2];
                    text.osStyle = osCurStyle;
                    text.osColour = osCurColour;
                    m_oModels[osCurModel].aoTexts.push_back(std::move(text));
                }
            }
        }

        // --- Coordinate lines inside string data ---
        if (bInString && bInData && !bInExtrudeValue)
        {
            if (IsCoordinateLine(pszLine))
            {
                auto aoVals = ExtractNumericValues(pszLine, 3);
                if (aoVals.size() >= 2)
                {
                    std::optional<double> odfZ;
                    if (aoVals.size() >= 3)
                        odfZ = aoVals[2];
                    else
                        odfZ = odfCurStringZ;
                    aoCurString.push_back(
                        MakePoint12da(aoVals[0], aoVals[1], odfZ, osCurStyle,
                                      osCurColour));
                }
            }
            else if (StartsWithKw(pszLine, "xy") ||
                     StartsWithKw(pszLine, "xyz") ||
                     StartsWithKw(pszLine, "pt") ||
                     StartsWithKw(pszLine, "point"))
            {
                auto aoNums = ExtractNumericValues(pszLine, 3);
                if (aoNums.size() >= 2)
                {
                    std::optional<double> odfZ;
                    if (aoNums.size() >= 3)
                        odfZ = aoNums[2];
                    else
                        odfZ = odfCurStringZ;
                    aoCurString.push_back(MakePoint12da(
                        aoNums[0], aoNums[1], odfZ, osCurStyle, osCurColour));
                }
            }
            else if (IsNumericishStart(pszLine))
            {
                auto aoNums = ExtractNumericValues(pszLine, 3);
                if (aoNums.size() == 2 || aoNums.size() == 3)
                {
                    std::optional<double> odfZ;
                    if (aoNums.size() == 3)
                        odfZ = aoNums[2];
                    else
                        odfZ = odfCurStringZ;
                    aoCurString.push_back(MakePoint12da(
                        aoNums[0], aoNums[1], odfZ, osCurStyle, osCurColour));
                }
            }
        }

        // --- Points outside string ---
        if (!bInString)
        {
            std::string osFirst;
            {
                const char *pF = pszLine;
                while (*pF && *pF != ' ' && *pF != '\t')
                    pF++;
                osFirst.assign(pszLine, pF - pszLine);
                for (auto &c : osFirst)
                    c = static_cast<char>(
                        tolower(static_cast<unsigned char>(c)));
            }

            if (osFirst == "vert" || osFirst == "vertex")
            {
                auto aoNums = ExtractNumericValues(pszLine, 4);
                if (aoNums.size() == 4)
                {
                    aoTinVerts.push_back(MakePoint12da(
                        aoNums[1], aoNums[2], aoNums[3], osCurStyle,
                        osCurColour));
                }
                else if (aoNums.size() >= 2)
                {
                    std::optional<double> odfZ;
                    if (aoNums.size() >= 3)
                        odfZ = aoNums[2];
                    aoTinVerts.push_back(MakePoint12da(
                        aoNums[0], aoNums[1], odfZ, osCurStyle, osCurColour));
                }
            }

            if (osFirst == "face" || osFirst == "tri" ||
                osFirst == "triangle")
            {
                auto aoNums = ExtractNumericValues(pszLine, 9);
                if (aoNums.size() == 9)
                {
                    // Inline coords
                    auto t1 = MakePoint12da(aoNums[0], aoNums[1], aoNums[2],
                                            osCurStyle, osCurColour);
                    auto t2 = MakePoint12da(aoNums[3], aoNums[4], aoNums[5],
                                            osCurStyle, osCurColour);
                    auto t3 = MakePoint12da(aoNums[6], aoNums[7], aoNums[8],
                                            osCurStyle, osCurColour);
                    m_oModels[osCurModel].aoTriangles.push_back(
                        {std::move(t1), std::move(t2), std::move(t3)});
                }
                else if (aoNums.size() >= 3)
                {
                    int ia = static_cast<int>(aoNums[0]);
                    int ib = static_cast<int>(aoNums[1]);
                    int ic = static_cast<int>(aoNums[2]);
                    if (ia >= 0 && ib >= 0 && ic >= 0 &&
                        ia < static_cast<int>(aoTinVerts.size()) &&
                        ib < static_cast<int>(aoTinVerts.size()) &&
                        ic < static_cast<int>(aoTinVerts.size()))
                    {
                        m_oModels[osCurModel].aoTriangles.push_back(
                            {aoTinVerts[ia], aoTinVerts[ib], aoTinVerts[ic]});
                    }
                }
            }

            if (StartsWithKw(pszLine, "xy") || StartsWithKw(pszLine, "xyz") ||
                StartsWithKw(pszLine, "pt") ||
                StartsWithKw(pszLine, "point"))
            {
                auto aoNums = ExtractNumericValues(pszLine, 3);
                if (aoNums.size() >= 2)
                {
                    std::optional<double> odfZ;
                    if (aoNums.size() >= 3)
                        odfZ = aoNums[2];
                    m_oModels[osCurModel].aoPts.push_back(MakePoint12da(
                        aoNums[0], aoNums[1], odfZ, osCurStyle, osCurColour));
                }
            }
            else if (IsCoordinateLine(pszLine) && !bInTin && !bInPrim3d)
            {
                auto aoVals = ExtractNumericValues(pszLine, 3);
                if (aoVals.size() >= 2)
                {
                    std::optional<double> odfZ;
                    if (aoVals.size() >= 3)
                        odfZ = aoVals[2];
                    m_oModels[osCurModel].aoPts.push_back(MakePoint12da(
                        aoVals[0], aoVals[1], odfZ, osCurStyle, osCurColour));
                }
            }
        }

        // --- TIN keywords ---
        if (!bInTin && (StartsWithKw(pszLine, "tin") ||
                        StartsWithKw(pszLine, "full_tin")))
        {
            bInTin = true;
            nTinBr = 0;
            bTinHasOpen = osLine.find('{') != std::string::npos;
            aoTinVerts.clear();
            aoTinFaces.clear();
            bInTinNulling = false;
            nTnBr = 0;
            aoTinNulling.clear();
            osTinName.reset();
            dfTinNullLength = 0.0;
            aoTinAttrs.clear();
            aoCurMatrix.clear();
        }

        if (bInTin)
        {
            if (StartsWithKw(pszLine, "name"))
            {
                auto aoQs = ParseQuotedStrings(pszLine);
                if (!aoQs.empty())
                    osTinName = aoQs[0];
            }

            if (!bInTinVerts && !bInTinFaces &&
                StartsWithKw(pszLine, "real"))
            {
                auto aoQs = ParseQuotedStrings(pszLine);
                if (!aoQs.empty())
                {
                    std::string osNameLower = aoQs[0];
                    for (auto &c : osNameLower)
                        c = static_cast<char>(
                            tolower(static_cast<unsigned char>(c)));
                    if (osNameLower == "null_length")
                    {
                        auto aoVals = ExtractNumericValues(pszLine, 3);
                        if (aoVals.size() >= 2)
                            dfTinNullLength = aoVals.back();
                    }
                }
            }

            if (!bInTinVerts && (StartsWithKw(pszLine, "vertices") ||
                                 StartsWithKw(pszLine, "points")))
            {
                bInTinVerts = true;
                nTvBr = 0;
            }
            if (!bInTinFaces && (StartsWithKw(pszLine, "faces") ||
                                 StartsWithKw(pszLine, "triangles")))
            {
                bInTinFaces = true;
                nTfBr = 0;
            }
            if (!bInTinNulling && StartsWithKw(pszLine, "nulling") &&
                osLine.find('{') != std::string::npos)
            {
                bInTinNulling = true;
                nTnBr = 0;
                aoTinNulling.clear();
            }
        }

        // --- TIN vertex parsing ---
        if (bInTinVerts && !StartsWithKw(pszLine, "vertices") &&
            !StartsWithKw(pszLine, "points"))
        {
            auto aoNums = ExtractNumericValues(pszLine, 12);
            if (aoNums.size() >= 3)
            {
                // Detect stride-4 (id xyz) vs stride-3 (xyz)
                bool bStride4 = (aoNums.size() % 4 == 0) &&
                                (aoNums.size() >= 4);
                if (bStride4)
                {
                    for (size_t i = 0; i + 3 < aoNums.size(); i += 4)
                    {
                        aoTinVerts.push_back(MakePoint12da(
                            aoNums[i + 1], aoNums[i + 2], aoNums[i + 3],
                            osCurStyle, osCurColour));
                    }
                }
                else
                {
                    for (size_t i = 0; i + 2 < aoNums.size(); i += 3)
                    {
                        aoTinVerts.push_back(
                            MakePoint12da(aoNums[i], aoNums[i + 1],
                                          aoNums[i + 2], osCurStyle,
                                          osCurColour));
                    }
                }
            }
        }

        // --- TIN face parsing ---
        if (bInTinFaces && !StartsWithKw(pszLine, "faces") &&
            !StartsWithKw(pszLine, "triangles"))
        {
            auto aoNums = ExtractNumericValues(pszLine, 12);
            for (size_t i = 0; i + 2 < aoNums.size(); i += 3)
            {
                aoTinFaces.push_back(
                    {static_cast<int>(aoNums[i]),
                     static_cast<int>(aoNums[i + 1]),
                     static_cast<int>(aoNums[i + 2])});
            }
        }

        // --- TIN nulling ---
        if (bInTinNulling && !StartsWithKw(pszLine, "nulling"))
        {
            auto aoNums = ExtractNumericValues(pszLine, 100);
            for (double v : aoNums)
                aoTinNulling.push_back(static_cast<int>(v));
        }

        // --- Transformation ---
        if ((bInTin || bInPrim3d) && !bInTransformation &&
            StartsWithKw(pszLine, "transformation"))
        {
            bInTransformation = true;
            nTransBr = 0;
            aoCurMatrix.clear();
            auto aoNums = ExtractNumericValues(pszLine, 16);
            aoCurMatrix.insert(aoCurMatrix.end(), aoNums.begin(),
                               aoNums.end());
        }
        if (bInTransformation)
        {
            if (!StartsWithKw(pszLine, "transformation"))
            {
                auto aoNums = ExtractNumericValues(pszLine, 16);
                aoCurMatrix.insert(aoCurMatrix.end(), aoNums.begin(),
                                   aoNums.end());
            }
        }

        // --- Primitive / trimesh ---
        if (!bInPrim3d && StartsWithKw(pszLine, "primitive_3d"))
        {
            bInPrim3d = true;
            nPrimBr = 0;
            bInTrimesh3d = false;
            bInTriVerts = false;
            nVBr = 0;
            aoTriVerts.clear();
            bInTriFaces = false;
            nFBr = 0;
            aoTriFaces.clear();
            aoCurMatrix.clear();
        }
        if (bInPrim3d && !bInTrimesh3d &&
            StartsWithKw(pszLine, "trimesh_3d"))
        {
            bInTrimesh3d = true;
        }
        if (bInPrim3d && bInTrimesh3d)
        {
            if (!bInTriVerts && (StartsWithKw(pszLine, "vertices") ||
                                 StartsWithKw(pszLine, "points")))
            {
                bInTriVerts = true;
                nVBr = 0;
            }
            if (!bInTriFaces && (StartsWithKw(pszLine, "faces") ||
                                 StartsWithKw(pszLine, "triangles")))
            {
                bInTriFaces = true;
                nFBr = 0;
            }
        }

        // Trimesh vertex parsing
        if (bInTriVerts && !StartsWithKw(pszLine, "vertices") &&
            !StartsWithKw(pszLine, "points") &&
            osLine.find('{') == std::string::npos &&
            osLine.find('}') == std::string::npos)
        {
            auto aoNums = ExtractNumericValues(pszLine, 4);
            if (aoNums.size() == 4)
            {
                aoTriVerts.push_back(
                    MakePoint12da(aoNums[1], aoNums[2], aoNums[3], osCurStyle,
                                  osCurColour));
            }
            else if (aoNums.size() >= 3)
            {
                aoTriVerts.push_back(
                    MakePoint12da(aoNums[0], aoNums[1], aoNums[2], osCurStyle,
                                  osCurColour));
            }
        }

        // Trimesh face parsing
        if (bInTriFaces && !StartsWithKw(pszLine, "faces") &&
            !StartsWithKw(pszLine, "triangles"))
        {
            auto aoNums = ExtractNumericValues(pszLine, 12);
            for (size_t i = 0; i + 2 < aoNums.size(); i += 3)
            {
                aoTriFaces.push_back(
                    {static_cast<int>(aoNums[i]),
                     static_cast<int>(aoNums[i + 1]),
                     static_cast<int>(aoNums[i + 2])});
            }
        }

        // === Brace tracking + finalize blocks ===

        if (bInString)
        {
            if (bInData)
            {
                nDataBr += nDelta;
                if (nDataBr <= 0)
                {
                    bInData = false;
                    nDataBr = 0;
                }
            }
            if (bInPointData)
            {
                nPdBr += nDelta;
                if (nPdBr <= 0)
                {
                    bInPointData = false;
                    nPdBr = 0;
                }
            }
            nStringBr += nDelta;
            if (nStringBr <= 0)
            {
                // Flush text string block
                if (bInTextString)
                {
                    if (odfTextX.has_value() && odfTextY.has_value() &&
                        !osTextLabel.empty())
                    {
                        TextEnt12da text;
                        text.osLabel = osTextLabel;
                        text.dfE = *odfTextX;
                        text.dfN = *odfTextY;
                        text.odfZ = odfTextZ;
                        text.osStyle = osCurStyle;
                        text.osColour = osCurColour;
                        text.odfWorldsize = odfTextWorldsize;
                        text.osTextstyle = osTextstyle;
                        text.odfAngle = odfTextAngle;
                        text.odfXFactor = odfTextXFactor;
                        if (!aoCurStringAttrs.empty())
                            text.aoAttributes = aoCurStringAttrs;
                        m_oModels[osCurModel].aoTexts.push_back(
                            std::move(text));
                    }
                    bInTextString = false;
                }

                // Flush string
                if (!aoCurString.empty())
                {
                    // Apply point IDs
                    if (aoCurPointIds.size() == aoCurString.size())
                    {
                        for (size_t i = 0; i < aoCurPointIds.size(); i++)
                            aoCurString[i].osPid = aoCurPointIds[i];
                    }
                    // Apply attributes to first point
                    if (!aoCurStringAttrs.empty())
                        aoCurString[0].aoAttributes = aoCurStringAttrs;

                    auto &oDest = m_oModels[osCurModel];
                    const bool bIsBreak =
                        bCurIsBreaklinePoint ||
                        std::any_of(aoCurString.begin(), aoCurString.end(),
                                    [](const Point12da &p)
                                    { return p.bIsBreaklinePoint; });
                    if (bIsBreak)
                    {
                        for (auto &pt : aoCurString)
                        {
                            oDest.aoPts.push_back(pt);
                        }
                    }
                    else if (bCurClosed && aoCurString.size() >= 3)
                    {
                        oDest.aoPolygons.push_back(aoCurString);
                    }
                    else if (aoCurString.size() == 1)
                    {
                        oDest.aoPts.push_back(aoCurString[0]);
                    }
                    else if (aoCurString.size() == 2)
                    {
                        // 2-point string: check if real line or just a marker
                        bool bHasValidZ = aoCurString[0].odfZ.has_value() &&
                                          aoCurString[1].odfZ.has_value();
                        double dfDx =
                            aoCurString[0].dfE - aoCurString[1].dfE;
                        double dfDy =
                            aoCurString[0].dfN - aoCurString[1].dfN;
                        double dfDist =
                            std::sqrt(dfDx * dfDx + dfDy * dfDy);
                        if (bHasValidZ && dfDist > 5.0)
                        {
                            oDest.aoPolylines.push_back(aoCurString);
                        }
                        else
                        {
                            oDest.aoPts.push_back(aoCurString[0]);
                        }
                    }
                    else if (aoCurString.size() >= 3)
                    {
                        oDest.aoPolylines.push_back(aoCurString);
                    }
                }

                bInString = false;
                nStringBr = 0;
                aoCurString.clear();
                bCurClosed = false;
                aoCurPointIds.clear();
                aoCurStringAttrs.clear();
                bCurIsBreaklinePoint = false;
                bInData = false;
                nDataBr = 0;
                bInPointData = false;
                nPdBr = 0;
            }
        }

        if (bInTin)
        {
            if (!bTinHasOpen && osRaw.find('{') != std::string::npos)
                bTinHasOpen = true;

            nTinBr += nDelta;
            if (bInTinVerts)
            {
                nTvBr += nDelta;
                if (nTvBr <= 0)
                {
                    bInTinVerts = false;
                    nTvBr = 0;
                }
            }
            if (bInTinFaces)
            {
                nTfBr += nDelta;
                if (nTfBr <= 0)
                {
                    bInTinFaces = false;
                    nTfBr = 0;
                }
            }
            if (bInTinNulling)
            {
                nTnBr += nDelta;
                if (nTnBr <= 0)
                {
                    bInTinNulling = false;
                    nTnBr = 0;
                }
            }
            if (bInTransformation)
            {
                nTransBr += nDelta;
                if (nTransBr <= 0)
                {
                    bInTransformation = false;
                    nTransBr = 0;
                }
            }

            if (bTinHasOpen && nTinBr <= 0)
            {
                // Finalize TIN
                const std::string &osTarget =
                    osTinName.value_or(osCurModel);
                auto &oDest = m_oModels[osTarget];

                // Apply transformation matrix
                if (aoCurMatrix.size() >= 16)
                {
                    for (auto &pt : aoTinVerts)
                    {
                        double x = pt.dfE, y = pt.dfN,
                               z = pt.odfZ.value_or(0.0);
                        pt.dfE = aoCurMatrix[0] * x + aoCurMatrix[1] * y +
                                 aoCurMatrix[2] * z + aoCurMatrix[3];
                        pt.dfN = aoCurMatrix[4] * x + aoCurMatrix[5] * y +
                                 aoCurMatrix[6] * z + aoCurMatrix[7];
                        double nz = aoCurMatrix[8] * x + aoCurMatrix[9] * y +
                                    aoCurMatrix[10] * z + aoCurMatrix[11];
                        pt.odfZ = nz;
                    }
                }

                // Build null set
                std::set<int> oNullSet(aoTinNulling.begin(),
                                       aoTinNulling.end());

                // Assemble triangles from faces + vertices
                for (size_t fi = 0; fi < aoTinFaces.size(); fi++)
                {
                    if (oNullSet.count(static_cast<int>(fi)))
                        continue;

                    int ia = aoTinFaces[fi][0];
                    int ib = aoTinFaces[fi][1];
                    int ic = aoTinFaces[fi][2];
                    if (ia < 0 || ib < 0 || ic < 0)
                        continue;
                    if (ia >= static_cast<int>(aoTinVerts.size()) ||
                        ib >= static_cast<int>(aoTinVerts.size()) ||
                        ic >= static_cast<int>(aoTinVerts.size()))
                        continue;

                    // Null-length check
                    if (dfTinNullLength > 0)
                    {
                        auto dist = [](const Point12da &a,
                                       const Point12da &b) -> double
                        {
                            double dx = a.dfE - b.dfE;
                            double dy = a.dfN - b.dfN;
                            return std::sqrt(dx * dx + dy * dy);
                        };
                        if (dist(aoTinVerts[ia], aoTinVerts[ib]) >
                                dfTinNullLength ||
                            dist(aoTinVerts[ib], aoTinVerts[ic]) >
                                dfTinNullLength ||
                            dist(aoTinVerts[ic], aoTinVerts[ia]) >
                                dfTinNullLength)
                            continue;
                    }

                    oDest.aoTriangles.push_back(
                        {aoTinVerts[ia], aoTinVerts[ib], aoTinVerts[ic]});
                }

                // Reset TIN state
                bInTin = false;
                nTinBr = 0;
                bTinHasOpen = false;
                aoTinVerts.clear();
                aoTinFaces.clear();
                bInTinVerts = false;
                nTvBr = 0;
                bInTinFaces = false;
                nTfBr = 0;
                bInTinNulling = false;
                nTnBr = 0;
                aoTinNulling.clear();
                osTinName.reset();
                dfTinNullLength = 0.0;
                aoTinAttrs.clear();
                aoCurMatrix.clear();
                bInTransformation = false;
                nTransBr = 0;
            }
        }

        if (bInPrim3d)
        {
            nPrimBr += nDelta;
            if (bInTriVerts)
            {
                nVBr += nDelta;
                if (nVBr <= 0)
                {
                    bInTriVerts = false;
                    nVBr = 0;
                }
            }
            if (bInTriFaces)
            {
                nFBr += nDelta;
                if (nFBr <= 0)
                {
                    bInTriFaces = false;
                    nFBr = 0;
                }
            }
            if (bInTransformation)
            {
                nTransBr += nDelta;
                if (nTransBr <= 0)
                {
                    bInTransformation = false;
                    nTransBr = 0;
                }
            }

            if (nPrimBr <= 0)
            {
                // Finalize trimesh
                auto &oDest = m_oModels[osCurModel];

                // Apply transformation
                if (aoCurMatrix.size() >= 16)
                {
                    for (auto &pt : aoTriVerts)
                    {
                        double x = pt.dfE, y = pt.dfN,
                               z = pt.odfZ.value_or(0.0);
                        pt.dfE = aoCurMatrix[0] * x + aoCurMatrix[1] * y +
                                 aoCurMatrix[2] * z + aoCurMatrix[3];
                        pt.dfN = aoCurMatrix[4] * x + aoCurMatrix[5] * y +
                                 aoCurMatrix[6] * z + aoCurMatrix[7];
                        double nz = aoCurMatrix[8] * x + aoCurMatrix[9] * y +
                                    aoCurMatrix[10] * z + aoCurMatrix[11];
                        pt.odfZ = nz;
                    }
                }

                for (const auto &face : aoTriFaces)
                {
                    int ia = face[0], ib = face[1], ic = face[2];
                    if (ia >= 0 && ib >= 0 && ic >= 0 &&
                        ia < static_cast<int>(aoTriVerts.size()) &&
                        ib < static_cast<int>(aoTriVerts.size()) &&
                        ic < static_cast<int>(aoTriVerts.size()))
                    {
                        oDest.aoTrimeshes.push_back(
                            {aoTriVerts[ia], aoTriVerts[ib], aoTriVerts[ic]});
                    }
                }

                bInPrim3d = false;
                nPrimBr = 0;
                bInTrimesh3d = false;
                bInTriVerts = false;
                nVBr = 0;
                aoTriVerts.clear();
                bInTriFaces = false;
                nFBr = 0;
                aoTriFaces.clear();
                bInTransformation = false;
                nTransBr = 0;
                aoCurMatrix.clear();
            }
        }
    }

    return true;
}

/************************************************************************/
/*                          CreateLayers()                             */
/************************************************************************/

void OGR12daDataSource::CreateLayers()
{
    for (auto &[osModelName, oModel] : m_oModels)
    {
        if (!oModel.aoPts.empty())
        {
            auto poLayer = std::make_unique<OGR12daLayer>(
                this, osModelName + "_Points", OGR12daLayerType::Points,
                osModelName, &oModel);
            m_apoLayers.push_back(std::move(poLayer));
        }
        if (!oModel.aoPolylines.empty())
        {
            auto poLayer = std::make_unique<OGR12daLayer>(
                this, osModelName + "_Lines", OGR12daLayerType::Lines,
                osModelName, &oModel);
            m_apoLayers.push_back(std::move(poLayer));
        }
        if (!oModel.aoPolygons.empty())
        {
            auto poLayer = std::make_unique<OGR12daLayer>(
                this, osModelName + "_Polygons", OGR12daLayerType::Polygons,
                osModelName, &oModel);
            m_apoLayers.push_back(std::move(poLayer));
        }
        if (!oModel.aoTriangles.empty())
        {
            auto poLayer = std::make_unique<OGR12daLayer>(
                this, osModelName + "_TIN", OGR12daLayerType::TIN,
                osModelName, &oModel);
            m_apoLayers.push_back(std::move(poLayer));
        }
        if (!oModel.aoTrimeshes.empty())
        {
            auto poLayer = std::make_unique<OGR12daLayer>(
                this, osModelName + "_Trimesh", OGR12daLayerType::Trimesh,
                osModelName, &oModel);
            m_apoLayers.push_back(std::move(poLayer));
        }
        if (!oModel.aoTexts.empty())
        {
            auto poLayer = std::make_unique<OGR12daLayer>(
                this, osModelName + "_Text", OGR12daLayerType::Text,
                osModelName, &oModel);
            m_apoLayers.push_back(std::move(poLayer));
        }
        if (!oModel.aoSuperAlignments.empty())
        {
            auto poLayer = std::make_unique<OGR12daLayer>(
                this, osModelName + "_SuperAlignments",
                OGR12daLayerType::SuperAlignments, osModelName, &oModel);
            m_apoLayers.push_back(std::move(poLayer));
        }
    }
}

/************************************************************************/
/*                            Identify()                               */
/************************************************************************/

int OGR12daDataSource::Identify(GDALOpenInfo *poOpenInfo)
{
    // Check extension
    const std::string osExt = CPLGetExtensionSafe(poOpenInfo->pszFilename);
    const char *pszExt = osExt.c_str();
    if (EQUAL(pszExt, "12daz"))
    {
        // Check for ZIP magic
        if (poOpenInfo->nHeaderBytes >= 4 &&
            poOpenInfo->pabyHeader[0] == 'P' &&
            poOpenInfo->pabyHeader[1] == 'K' &&
            poOpenInfo->pabyHeader[2] == 0x03 &&
            poOpenInfo->pabyHeader[3] == 0x04)
        {
            return TRUE;
        }
        return FALSE;
    }

    if (!EQUAL(pszExt, "12da"))
        return FALSE;

    // Check content for 12da markers
    if (poOpenInfo->nHeaderBytes < 10)
        return FALSE;

    const char *pszHeader =
        reinterpret_cast<const char *>(poOpenInfo->pabyHeader);
    const int nCheckLen =
        std::min(poOpenInfo->nHeaderBytes, 4096);
    std::string osHeader(pszHeader, nCheckLen);

    // Check for "null null" header or common 12da keywords
    if (osHeader.find("null null") != std::string::npos ||
        osHeader.find("model ") != std::string::npos ||
        osHeader.find("string ") != std::string::npos ||
        osHeader.find("tin ") != std::string::npos ||
        osHeader.find("data ") != std::string::npos ||
        osHeader.find("super string") != std::string::npos)
    {
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/
/*                              Open()                                 */
/************************************************************************/

GDALDataset *OGR12daDataSource::Open(GDALOpenInfo *poOpenInfo)
{
    if (!Identify(poOpenInfo))
        return nullptr;

    if (poOpenInfo->eAccess == GA_Update)
    {
        CPLError(CE_Failure, CPLE_NotSupported,
                 "12DA driver does not support update access.");
        return nullptr;
    }

    // Determine the actual file to read
    std::string osFilename = poOpenInfo->pszFilename;
    const std::string osExt = CPLGetExtensionSafe(osFilename.c_str());
    const char *pszExt = osExt.c_str();

    std::string osText;

    if (EQUAL(pszExt, "12daz"))
    {
        // ZIP compressed - find the .12da inside
        std::string osZipPath =
            CPLSPrintf("/vsizip/%s", osFilename.c_str());
        char **papszFiles = VSIReadDir(osZipPath.c_str());
        if (papszFiles == nullptr)
        {
            CPLError(CE_Failure, CPLE_OpenFailed,
                     "Cannot open ZIP archive: %s", osFilename.c_str());
            return nullptr;
        }

        std::string os12daFile;
        vsi_l_offset nLargestSize = 0;
        for (int i = 0; papszFiles[i] != nullptr; i++)
        {
            if (EQUAL(CPLGetExtensionSafe(papszFiles[i]).c_str(), "12da"))
            {
                std::string osInnerPath =
                    CPLFormFilenameSafe(osZipPath.c_str(), papszFiles[i], nullptr);
                VSIStatBufL sStat;
                if (VSIStatL(osInnerPath.c_str(), &sStat) == 0 &&
                    static_cast<vsi_l_offset>(sStat.st_size) > nLargestSize)
                {
                    nLargestSize = sStat.st_size;
                    os12daFile = osInnerPath;
                }
            }
        }
        CSLDestroy(papszFiles);

        if (os12daFile.empty())
        {
            CPLError(CE_Failure, CPLE_OpenFailed,
                     "No .12da file found in ZIP: %s", osFilename.c_str());
            return nullptr;
        }

        // Read the inner file
        GByte *pabyData = nullptr;
        vsi_l_offset nSize = 0;
        pabyData = VSIGetMemFileBuffer(os12daFile.c_str(), &nSize, FALSE);
        if (pabyData == nullptr)
        {
            VSILFILE *fp = VSIFOpenL(os12daFile.c_str(), "rb");
            if (fp == nullptr)
                return nullptr;
            VSIFSeekL(fp, 0, SEEK_END);
            nSize = VSIFTellL(fp);
            VSIFSeekL(fp, 0, SEEK_SET);
            std::string osBuf(static_cast<size_t>(nSize), '\0');
            VSIFReadL(&osBuf[0], 1, static_cast<size_t>(nSize), fp);
            VSIFCloseL(fp);
            osText = std::move(osBuf);
        }
        else
        {
            osText.assign(reinterpret_cast<const char *>(pabyData),
                          static_cast<size_t>(nSize));
        }
    }
    else
    {
        // Read the .12da file directly
        VSILFILE *fp = VSIFOpenL(osFilename.c_str(), "rb");
        if (fp == nullptr)
            return nullptr;
        VSIFSeekL(fp, 0, SEEK_END);
        vsi_l_offset nSize = VSIFTellL(fp);
        VSIFSeekL(fp, 0, SEEK_SET);
        osText.resize(static_cast<size_t>(nSize));
        VSIFReadL(&osText[0], 1, static_cast<size_t>(nSize), fp);
        VSIFCloseL(fp);
    }

    // Handle UTF-16LE BOM
    if (osText.size() >= 2 &&
        static_cast<unsigned char>(osText[0]) == 0xFF &&
        static_cast<unsigned char>(osText[1]) == 0xFE)
    {
        // Convert UTF-16LE to UTF-8
        std::string osConverted;
        osConverted.reserve(osText.size());
        for (size_t i = 2; i + 1 < osText.size(); i += 2)
        {
            unsigned int c =
                static_cast<unsigned char>(osText[i]) |
                (static_cast<unsigned char>(osText[i + 1]) << 8);
            if (c < 0x80)
            {
                osConverted += static_cast<char>(c);
            }
            else if (c < 0x800)
            {
                osConverted += static_cast<char>(0xC0 | (c >> 6));
                osConverted += static_cast<char>(0x80 | (c & 0x3F));
            }
            else
            {
                osConverted += static_cast<char>(0xE0 | (c >> 12));
                osConverted += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                osConverted += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        osText = std::move(osConverted);
    }

    auto poDS = std::make_unique<OGR12daDataSource>();
    poDS->SetDescription(poOpenInfo->pszFilename);

    if (!poDS->Parse(osText.c_str()))
        return nullptr;

    poDS->CreateLayers();

    return poDS.release();
}
