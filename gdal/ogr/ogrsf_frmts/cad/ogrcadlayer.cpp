/*******************************************************************************
 *  Project: OGR CAD Driver
 *  Purpose: Implements driver based on libopencad
 *  Author: Alexandr Borzykh, mush3d at gmail.com
 *  Language: C++
 *******************************************************************************
 *  The MIT License (MIT)
 *
 *  Copyright (c) 2016 Alexandr Borzykh
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *******************************************************************************/
#include "ogr_cad.h"
#include "cpl_conv.h"

OGRCADLayer::OGRCADLayer( CADLayer &poCADLayer_, std::string sESRISpatRef ) :
	poCADLayer( poCADLayer_ )
{
	nNextFID = 0;
    poSpatialRef = NULL;

    poFeatureDefn = new OGRFeatureDefn( CPLGetBasename( poCADLayer.getName().c_str() ) );

    OGRFieldDefn  oClassField( "Geometry", OFTString );
    poFeatureDefn->AddFieldDefn( &oClassField );
    
    OGRFieldDefn  oLinetypeField( "Thickness", OFTReal );
    poFeatureDefn->AddFieldDefn( &oLinetypeField );
  
    OGRFieldDefn  oColorField( "Color (RGB)", OFTIntegerList );
    poFeatureDefn->AddFieldDefn( &oColorField );

    OGRFieldDefn  oExtendedField( "ExtendedEntityData", OFTString );
    poFeatureDefn->AddFieldDefn( &oExtendedField );

    OGRFieldDefn  oTextField( "Text", OFTString );
    poFeatureDefn->AddFieldDefn( &oTextField );


    // Applying spatial ref info
    char ** papszPRJ = new char*[1];
    papszPRJ[0] = new char[sESRISpatRef.size()];
    memcpy( papszPRJ[0], sESRISpatRef.data(), sESRISpatRef.size() );
    if ( sESRISpatRef.size() != 0 )
    {
        poSpatialRef = new OGRSpatialReference();
        if ( poSpatialRef->importFromESRI( papszPRJ ) != OGRERR_NONE )
        {
            CPLError( CE_Warning, CPLE_AppDefined,
                        "Failed to parse PRJ section, ignoring." );
            delete( poSpatialRef );
            poSpatialRef = NULL;
        }
    }
    delete[] papszPRJ;

    SetDescription( poFeatureDefn->GetName() );
    poFeatureDefn->Reference();
}

GIntBig OGRCADLayer::GetFeatureCount( int bForce )
{
    bForce = false; // FIXME: fix warning only
    return poCADLayer.getGeometryCount();
}

OGRCADLayer::~OGRCADLayer()
{
	poFeatureDefn->Release();
}

void OGRCADLayer::ResetReading()
{
	nNextFID = 0;
}

OGRFeature *OGRCADLayer::GetNextFeature()
{
    OGRFeature *apoFeature = GetFeature( nNextFID );
    ++nNextFID;
    return apoFeature;
}

OGRFeature *OGRCADLayer::GetFeature( GIntBig nFID )
{
    if( (int)poCADLayer.getGeometryCount() <= nFID )
    {
        return NULL;
    }
    
    OGRFeature  *poFeature = NULL;
    std::unique_ptr<CADGeometry> spoCADGeometry(poCADLayer.getGeometry( nFID ) );
    
    if( GetLastErrorCode() != CADErrorCodes::SUCCESS )
    {
        CPLError( CE_Failure, CPLE_NotSupported,
                 "Failed to get geometry with ID = %d from layer \"%s\". Libopencad errorcode: %d",
                 (int)nFID, poCADLayer.getName().c_str(), GetLastErrorCode() );
        return NULL;
    }
    
    poFeature = new OGRFeature( poFeatureDefn );
    poFeature->SetFID( static_cast<int>( nFID ) );
    poFeature->SetField( "Thickness", spoCADGeometry->getThickness() );

    if( spoCADGeometry->getEED().size() != 0 )
    {
        std::string sEEDAsOneString = "";
        for ( auto iter = spoCADGeometry->getEED().cbegin();
              iter != spoCADGeometry->getEED().cend(); ++iter )
        {
            sEEDAsOneString += *iter;
            sEEDAsOneString += ' ';
        }

        poFeature->SetField( "ExtendedEntityData", sEEDAsOneString.c_str() );
    }
    
    RGBColor stRGB = spoCADGeometry->getColor();
    GIntBig adRGB[3] { stRGB.R, stRGB.G, stRGB.B };
    poFeature->SetField( "Color (RGB)", 3, adRGB);
    
    switch( spoCADGeometry->getType() )
    {
        case CADGeometry::POINT:
        {
            CADPoint3D * const poCADPoint = ( CADPoint3D* ) spoCADGeometry.get();
            CADVector stPositionVector = poCADPoint->getPosition();
            
            poFeature->SetGeometryDirectly( new OGRPoint( stPositionVector.getX(),
                                                         stPositionVector.getY(), stPositionVector.getZ() ) );
            poFeature->SetField( "Geometry", "CADPoint" );
            return poFeature;
        }
        
        case CADGeometry::LINE:
        {
            CADLine * const poCADLine = ( CADLine* ) spoCADGeometry.get();
            OGRLineString *poLS = new OGRLineString();
            poLS->addPoint( poCADLine->getStart().getPosition().getX(),
                           poCADLine->getStart().getPosition().getY(),
                           poCADLine->getStart().getPosition().getZ() );
            poLS->addPoint( poCADLine->getEnd().getPosition().getX(),
                           poCADLine->getEnd().getPosition().getY(),
                           poCADLine->getEnd().getPosition().getZ() );
            
            poFeature->SetGeometryDirectly( poLS );
            poFeature->SetField( "Geometry", "CADLine" );
            return poFeature;
        }

        case CADGeometry::CIRCLE:
        {
            CADCircle * const poCADCircle = ( CADCircle* ) spoCADGeometry.get();
            OGRGeometry *poCircle = OGRGeometryFactory::approximateArcAngles(
                    poCADCircle->getPosition().getX(), poCADCircle->getPosition().getY(),
                    poCADCircle->getPosition().getZ(),
                    poCADCircle->getRadius(), poCADCircle->getRadius(), 0.0,
                    0.0, 360.0,
                    0.0 );
            poFeature->SetGeometryDirectly( poCircle );
            
            poFeature->SetField( "Geometry", "CADCircle" );
            return poFeature;
        }
        
        case CADGeometry::ARC:
        {
            CADArc * const poCADArc = ( CADArc* ) spoCADGeometry.get();
            OGRGeometry *poArc = OGRGeometryFactory::approximateArcAngles(
                poCADArc->getPosition().getX(), poCADArc->getPosition().getY(),
                poCADArc->getPosition().getZ(),
                poCADArc->getRadius(), poCADArc->getRadius(), 0.0,
                poCADArc->getStartingAngle(), 
                poCADArc->getEndingAngle() < poCADArc->getStartingAngle() ?
                    poCADArc->getEndingAngle() : ( poCADArc->getEndingAngle() + 360.0f ),
                0.0 );

            poFeature->SetGeometryDirectly( poArc );

            poFeature->SetField( "Geometry", "CADArc" );
            return poFeature;
        }

        case CADGeometry::FACE3D:
        {
            CADFace3D * const poCADFace = ( CADFace3D* ) spoCADGeometry.get();
            OGRPolygon * poPoly = new OGRPolygon();
            OGRLinearRing * poLR = new OGRLinearRing();

            for ( size_t i = 0; i < 3; ++i )
            {
                poLR->addPoint(
                    poCADFace->getCorner( i ).getX(),
                    poCADFace->getCorner( i ).getY(),
                    poCADFace->getCorner( i ).getZ()
                );
            }
            if ( !(poCADFace->getCorner( 2 ) == poCADFace->getCorner( 3 )) )
            {
                poLR->addPoint(
                    poCADFace->getCorner( 3 ).getX(),
                    poCADFace->getCorner( 3 ).getY(),
                    poCADFace->getCorner( 3 ).getZ()
                );
            }
            poPoly->addRingDirectly( poLR );
            poPoly->closeRings();
            poFeature->SetGeometryDirectly( poPoly );

            poFeature->SetField( "Geometry", "CADFace3D" );
            return poFeature;
        }

        // TODO: unsupported smooth lines
        case CADGeometry::LWPOLYLINE:
        {
            CADLWPolyline * const poCADLWPolyline = ( CADLWPolyline* ) spoCADGeometry.get();
            OGRLineString * poLS = new OGRLineString();

            for( size_t i = 0; i < poCADLWPolyline->getVertexCount(); ++i )
            {
                CADVector stVertex = poCADLWPolyline->getVertex( i );

                poLS->addPoint( stVertex.getX(),
                                stVertex.getY(),
                                stVertex.getZ() );
            }

            poFeature->SetGeometryDirectly( poLS );
            poFeature->SetField( "Geometry", "CADLWPolyline" );
            return poFeature;
        }

        // TODO: unsupported smooth lines
        case CADGeometry::POLYLINE3D:
        {
            CADPolyline3D * const poCADPolyline3D = ( CADPolyline3D* ) spoCADGeometry.get();
            OGRLineString * poLS = new OGRLineString();

            for( size_t i = 0; i < poCADPolyline3D->getVertexCount(); ++i )
            {
                CADVector stVertex = poCADPolyline3D->getVertex( i );

                poLS->addPoint( stVertex.getX(),
                                stVertex.getY(),
                                stVertex.getZ() );
            }
            
            poFeature->SetGeometryDirectly( poLS );
            poFeature->SetField( "Geometry", "CADPolyline3D" );
            return poFeature;
        }

        case CADGeometry::TEXT:
        {
            CADText * const poCADText = ( CADText * ) spoCADGeometry.get();
            OGRPoint * poPoint = new OGRPoint( poCADText->getPosition().getX(),
                                               poCADText->getPosition().getY(),
                                               poCADText->getPosition().getZ() );
            poFeature->SetField( "Text", poCADText->getTextValue().c_str() );

            poFeature->SetGeometryDirectly( poPoint );
            poFeature->SetField( "Geometry", "CADText" );
            return poFeature;
        }

        case CADGeometry::MTEXT:
        {
            CADMText * const poCADMText = ( CADMText * ) spoCADGeometry.get();
            OGRPoint * poPoint = new OGRPoint( poCADMText->getPosition().getX(),
                                               poCADMText->getPosition().getY(),
                                               poCADMText->getPosition().getZ() );
            poFeature->SetField( "Text", poCADMText->getTextValue().c_str() );

            poFeature->SetGeometryDirectly( poPoint );
            poFeature->SetField( "Geometry", "CADMText" );
            return poFeature;
        }

        case CADGeometry::ELLIPSE:
        {
            CADEllipse * const poCADEllipse = ( CADEllipse* ) spoCADGeometry.get();

            // FIXME: start/end angles should be swapped to work exactly as DXF driver.
            // is it correct?
            double dfStartAngle = -1 * poCADEllipse->getEndingAngle()
                                     * 180 / M_PI;
            double dfEndAngle = -1 * poCADEllipse->getStartingAngle()
                                     * 180 / M_PI;
            double dfAxisRatio = poCADEllipse->getAxisRatio();

            if( dfStartAngle > dfEndAngle )
                dfEndAngle += 360.0;

            CADVector vectPosition = poCADEllipse->getPosition();
            CADVector vectSMAxis = poCADEllipse->getSMAxis();
            double dfPrimaryRadius, dfSecondaryRadius;
            double dfRotation;
            dfPrimaryRadius = sqrt( vectSMAxis.getX() * vectSMAxis.getX()
                                    + vectSMAxis.getY() * vectSMAxis.getY()
                                    + vectSMAxis.getZ() * vectSMAxis.getZ() );

            dfSecondaryRadius = dfAxisRatio * dfPrimaryRadius;

            dfRotation = -1 * atan2( vectSMAxis.getY(), vectSMAxis.getX() ) * 180 / M_PI;

            OGRGeometry *poEllipse =
                OGRGeometryFactory::approximateArcAngles(
                    vectPosition.getX(), vectPosition.getY(), vectPosition.getZ(),
                    dfPrimaryRadius, dfSecondaryRadius, dfRotation,
                    dfStartAngle, dfEndAngle, 0.0 );

            poFeature->SetGeometryDirectly( poEllipse );
            poFeature->SetField( "Geometry", "CADEllipse" );
            return poFeature;
        }
        
        case CADGeometry::ATTDEF:
        {
            CADAttdef * const poCADAttdef = ( CADAttdef* ) spoCADGeometry.get();

            OGRPoint * poPoint = new OGRPoint( poCADAttdef->getPosition().getX(),
                                               poCADAttdef->getPosition().getY(),
                                               poCADAttdef->getPosition().getZ() );

            poFeature->SetField( "Text", poCADAttdef->getTag().c_str() );

            poFeature->SetGeometryDirectly( poPoint );
            poFeature->SetField( "Geometry", "CADAttdef" );
            return poFeature;
        }

        // case CADGeometry::SPLINE:
        // {
        //     CADSpline * const poCADSpline = ( CADSpline* ) spoCADGeometry.get();
        //     OGRLineString * poLS = new OGRLineString();

        //     poFeature->SetGeometryDirectly( poLS );
        //     poFeature->SetField( "Geometry", "CADSpline" );
        //     return poFeature;
        // }
            
        default:
        {
            CPLError( CE_Failure, CPLE_NotSupported,
                     "Unhandled feature. Skipping it." );
            
            poFeature->SetField( "Geometry", "Unhandled" );
            return poFeature;
        }
    }
    
    return NULL;
}