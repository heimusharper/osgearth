/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2010 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include <osgEarthAnnotation/LocalizedNode>
#include <osgEarthAnnotation/Decluttering>
#include <osgEarth/Utils>
#include <osgEarth/MapNode>
#include <osg/AutoTransform>
#include <osg/MatrixTransform>

using namespace osgEarth;
using namespace osgEarth::Annotation;


LocalizedNode::LocalizedNode(MapNode*                mapNode,
                             const GeoPoint&         position,
                             bool                    is2D ) :
PositionedAnnotationNode( mapNode ),
_mapSRS        ( mapNode ? mapNode->getMapSRS() : 0L ),
_horizonCulling( false ),
_autoTransform ( is2D ),
_scale         ( 1.0f, 1.0f, 1.0f )
{
    if ( _autoTransform )
    {
        osg::AutoTransform* at = new osg::AutoTransform();
        at->setAutoRotateMode( osg::AutoTransform::ROTATE_TO_SCREEN );
        at->setAutoScaleToScreen( true );
        at->setCullingActive( false ); // just for the first pass
        _xform = at;
    }
    else
    {
        _xform = new osg::MatrixTransform();
    }
    _xform->getOrCreateStateSet()->setMode( GL_LIGHTING, 0 );

    if ( _mapSRS.valid() )
    {
        setHorizonCulling( true );
    }
    
    setPosition( position );
}

void
LocalizedNode::traverse( osg::NodeVisitor& nv )
{
    if ( _autoTransform && nv.getVisitorType() == osg::NodeVisitor::CULL_VISITOR )
    {
       _xform->setCullingActive( true );
    }
    
    AnnotationNode::traverse( nv );
}

bool
LocalizedNode::setPosition( const osg::Vec3d& position )
{
    return setPosition( GeoPoint(_mapSRS.get(), position) );
}

bool
LocalizedNode::setPosition( const GeoPoint& pos )
{
    // first transform the point to the map's SRS:
    GeoPoint mapPos = _mapSRS.get() ? pos.transform(_mapSRS.get()) : pos;
    if ( !mapPos.isValid() )
        return false;

    // store it:
    _mapPosition = mapPos;

    // make sure the node is set up for auto-z-update if necessary:
    configureForAltitudeMode( pos.altitudeMode() );

    // update the node.
    if ( !updateTransforms( _mapPosition ) )
        return false;

    return true;
}

void
LocalizedNode::setScale( const osg::Vec3f& scale )
{
    _scale = scale;
    updateTransforms( getPosition() );
}

bool
LocalizedNode::updateTransforms( const GeoPoint& p, osg::Node* patch )
{
    GeoPoint absPos(p);
    if ( !makeAbsolute(absPos, patch) )
        return false;

    // update the transform:
    if ( _mapSRS.valid() )
    {
        osg::Matrixd local2world;
        absPos.createLocalToWorld( local2world );

        if ( _autoTransform )
        {
            static_cast<osg::AutoTransform*>(_xform.get())->setPosition( local2world.getTrans() );
            static_cast<osg::AutoTransform*>(_xform.get())->setScale( _scale );
        }
        else
        {
            static_cast<osg::MatrixTransform*>(_xform.get())->setMatrix( 
                osg::Matrix::scale(_scale) * local2world  );
        }

        
        CullNodeByHorizon* culler = dynamic_cast<CullNodeByHorizon*>(_xform->getCullCallback());
        if ( culler )
            culler->_world = local2world.getTrans();
    }
    else
    {
        if ( _autoTransform )
        {
            static_cast<osg::AutoTransform*>(_xform.get())->setPosition( absPos.vec3d() );
            static_cast<osg::AutoTransform*>(_xform.get())->setScale( _scale );
        }
        else
        {
            static_cast<osg::MatrixTransform*>(_xform.get())->setMatrix(
                osg::Matrix::scale(_scale) * osg::Matrix::translate(absPos.vec3d()) );
        }
    }

    return true;
}

GeoPoint
LocalizedNode::getPosition() const
{
    return _mapPosition;
}

void
LocalizedNode::setHorizonCulling( bool value )
{
    if ( _horizonCulling != value && _mapSRS.valid() )
    {
        _horizonCulling = value;

        if ( _horizonCulling )
        {
            osg::Vec3d world;
            if ( _autoTransform )
                world = static_cast<osg::AutoTransform*>(_xform.get())->getPosition();
            else
                world = static_cast<osg::MatrixTransform*>(_xform.get())->getMatrix().getTrans();

            _xform->setCullCallback( new CullNodeByHorizon(world, _mapSRS->getEllipsoid()) );
        }
        else
        {
            _xform->removeCullCallback( _xform->getCullCallback() );
        }
    }
}

void
LocalizedNode::reclamp( const TileKey& key, osg::Node* tile, const Terrain* terrain )
{
    // first verify that the control position intersects the tile:
    if ( key.getExtent().contains( _mapPosition.x(), _mapPosition.y() ) )
    {
        updateTransforms(_mapPosition, tile);
    }
}
