/******************************************************************************
 *
 * Project:  OGR 12da Driver
 * Purpose:  Implements driver registration for 12d Model ASCII format.
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
/*                         RegisterOGR12da()                           */
/************************************************************************/

void RegisterOGR12da()
{
    if (GDALGetDriverByName("12DA") != nullptr)
        return;

    GDALDriver *poDriver = new GDALDriver();

    poDriver->SetDescription("12DA");
    poDriver->SetMetadataItem(GDAL_DCAP_VECTOR, "YES");
    poDriver->SetMetadataItem(GDAL_DMD_LONGNAME,
                              "12d Model ASCII (.12da/.12daz)");
    poDriver->SetMetadataItem(GDAL_DMD_EXTENSIONS, "12da 12daz");
    poDriver->SetMetadataItem(GDAL_DMD_HELPTOPIC,
                              "drivers/vector/12da.html");
    poDriver->SetMetadataItem(GDAL_DCAP_VIRTUALIO, "YES");

    poDriver->pfnOpen = OGR12daDataSource::Open;
    poDriver->pfnIdentify = OGR12daDataSource::Identify;

    GetGDALDriverManager()->RegisterDriver(poDriver);
}
