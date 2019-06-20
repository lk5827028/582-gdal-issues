#include "netcdfsgwriterutil.h"
#include "netcdfdataset.h"

namespace nccfdriver
{
	OGR_SGeometry_Scribe GeometryScribe;

	SGeometry_Feature::SGeometry_Feature(OGRFeature& ft)
	{
		this->hasInteriorRing = false;
		OGRGeometry * defnGeometry = ft.GetGeometryRef();
		
		if (defnGeometry == nullptr)
		{
			// throw exception
		}
		
		OGRGeometry& r_defnGeometry = *defnGeometry;

		OGRwkbGeometryType ogwkt = defnGeometry->getGeometryType();
		this->type = OGRtoRaw(ogwkt);


		if (this->type == MULTIPOINT)
		{
			OGRMultiPoint& r_defnGeometryMP = dynamic_cast<OGRMultiPoint&>(r_defnGeometry);

			// Set total node count
			this->total_point_count = r_defnGeometryMP.getNumGeometries();

			// The amount of nodes is also the amount of parts
			for(size_t pc = 0; pc < total_point_count; pc++)
			{
				ppart_node_count.push_back(1);
			}

			// total part count ==  total node count
			this->total_part_count = this->total_point_count;	

		}

		else if (this->type == LINE)
		{		
			OGRLineString& r_defnGeometryLine = dynamic_cast<OGRLineString&>(r_defnGeometry);
			// to do: check for std::bad_cast somewhere?

			// Get node count
			this->total_point_count = r_defnGeometryLine.getNumPoints();
			
			// Single line: 1 part count == node count
			this->ppart_node_count.push_back(r_defnGeometryLine.getNumPoints());

			// One part
			this->total_part_count = 1;
		}

		else if(this->type == MULTILINE)
		{
			OGRMultiLineString& r_defnMLS = dynamic_cast<OGRMultiLineString&>(r_defnGeometry);
			this->total_point_count = 0;
			this->total_part_count = r_defnMLS.getNumGeometries();

			// Take each geometry, just add up the corresponding parts

			for(int itrLScount = 0; itrLScount < r_defnMLS.getNumGeometries(); itrLScount++)
			{
				OGRLineString & r_LS = dynamic_cast<OGRLineString&>(*r_defnMLS.getGeometryRef(itrLScount));
				int pt_count = r_LS.getNumPoints();
				
				this->ppart_node_count.push_back(pt_count);
				this->total_point_count += pt_count;
			}
		}

		else if(this->type == POLYGON)
		{
			OGRPolygon& r_defnPolygon = dynamic_cast<OGRPolygon&>(r_defnGeometry);

			this->total_point_count = 0;
			this->total_part_count = 0;

			// Get node count
			// First count exterior ring
			OGRLinearRing & counting_ring = *r_defnPolygon.getExteriorRing();

			size_t outer_ring_ct = counting_ring.getNumPoints();
		
			this->total_point_count += outer_ring_ct;
			this->ppart_node_count.push_back(outer_ring_ct);
			this->total_part_count++;

			// Then count all from the interior rings
			// While doing the following
			// Get per part node count (per part RING count)
			// Get part count (in this case it's the amount of RINGS)

			for(int iRingCt = 0; iRingCt < r_defnPolygon.getNumInteriorRings(); iRingCt++)
			{
				this->hasInteriorRing = true;
				counting_ring = *r_defnPolygon.getInteriorRing(iRingCt);
				this->total_point_count += counting_ring.getNumPoints();
				this->ppart_node_count.push_back(counting_ring.getNumPoints());
				this->total_part_count++;
			}
		}

		else if(this->type == MULTIPOLYGON)
		{

			OGRMultiPolygon& r_defnMPolygon = dynamic_cast<OGRMultiPolygon&>(r_defnGeometry);

			this->total_point_count = 0;
			this->total_part_count = 0;

			for(int itr = 0; itr < r_defnMPolygon.getNumGeometries(); itr++)
			{
				OGRPolygon & r_Pgon = dynamic_cast<OGRPolygon&>(*r_defnMPolygon.getGeometryRef(itr));
				
				OGRLinearRing & counting_ring = *r_Pgon.getExteriorRing();

				size_t outer_ring_ct = counting_ring.getNumPoints();
		
				this->total_point_count += outer_ring_ct;
				this->ppart_node_count.push_back(outer_ring_ct);
				this->total_part_count++;
				this->part_at_ind_interior.push_back(false);

				// Then count all from the interior rings
				// While doing the following
				// Get per part node count (per part RING count)
				// Get part count (in this case it's the amount of RINGS)

				for(int iRingCt = 0; iRingCt < r_Pgon.getNumInteriorRings(); iRingCt++)
				{
					this->hasInteriorRing = true;
					counting_ring = *r_Pgon.getInteriorRing(iRingCt);
					this->total_point_count += counting_ring.getNumPoints();
					this->ppart_node_count.push_back(counting_ring.getNumPoints());
					this->total_part_count++;
					this->part_at_ind_interior.push_back(true);
				}

			}

		}

		else
		{
			// throw an exception!
		}
			
		this->geometry_ref = ft.GetGeometryRef();
	}

	OGRPoint& SGeometry_Feature::getPoint(size_t part_no, int point_index)
	{
		if (this->type == MULTIPOINT)
		{
			OGRMultiPoint* as_mp_ref = dynamic_cast<OGRMultiPoint*>(this->geometry_ref);
			OGRPoint * pt = dynamic_cast<OGRPoint*>(as_mp_ref->getGeometryRef(part_no));
			return *pt;
		}

		if (this->type == LINE)
		{
			OGRLineString* as_line_ref = dynamic_cast<OGRLineString*>(this->geometry_ref);
			as_line_ref->getPoint(point_index, &pt_buffer);
		}

		if (this->type == MULTILINE)
		{
			OGRMultiLineString* as_mline_ref = dynamic_cast<OGRMultiLineString*>(this->geometry_ref);
			int part_ind = static_cast<int>(part_no);	
			OGRLineString* lstring = dynamic_cast<OGRLineString*>(as_mline_ref->getGeometryRef(part_ind));			
			lstring->getPoint(point_index, &pt_buffer);
		}

		if (this->type == POLYGON)
		{
			OGRPolygon* as_polygon_ref = dynamic_cast<OGRPolygon*>(this->geometry_ref);

			if(part_no == 0)
			{
				as_polygon_ref->getExteriorRing()->getPoint(point_index, &pt_buffer);
			}

			else
			{
				as_polygon_ref->getInteriorRing(part_no - 1)->getPoint(point_index, &pt_buffer);
			}
		}

		if (this->type == MULTIPOLYGON)
		{
			OGRMultiPolygon* as_mpolygon_ref = dynamic_cast<OGRMultiPolygon*>(this->geometry_ref);

			int polygon_num = 0;
			int ring_number = 0;

			// Find the right polygon, and the right ring number
			for(int pind = 0; pind < as_mpolygon_ref->getNumGeometries(); pind++)
			{
				OGRPolygon * itr_poly = dynamic_cast<OGRPolygon*>(as_mpolygon_ref->getGeometryRef(pind));
				if(part_no < (itr_poly->getNumInteriorRings() + 1)) // + 1 is counting the EXTERIOR ring
				{
					ring_number = static_cast<int>(part_no);	
				}

				else
				{
					part_no -= (itr_poly->getNumInteriorRings() + 1);
					polygon_num++;
				}
			}

			OGRPolygon* key_polygon = dynamic_cast<OGRPolygon*>(as_mpolygon_ref->getGeometryRef(polygon_num));
	
			if(ring_number == 0)
			{
				key_polygon->getExteriorRing()->getPoint(point_index, &pt_buffer);
			}

			else
			{
				key_polygon->getInteriorRing(ring_number - 1)->getPoint(point_index, &pt_buffer);
			}
		}

		return pt_buffer;
	}

	void OGR_SGeometry_Scribe::writeSGeometryFeature(SGeometry_Feature& ft)
	{
		if (ft.getType() == NONE)
			return;	// change to exception

		
		// Prepare variable names
		char node_coord_names[NC_MAX_CHAR + 1];
		memset(node_coord_names, 0, NC_MAX_CHAR + 1);

		char node_count_name[NC_MAX_CHAR + 1];
		memset(node_count_name, 0, NC_MAX_CHAR + 1);

		int err_code;
		err_code = nc_get_att_text(ncID, containerVarID, CF_SG_NODE_COORDINATES, node_coord_names);
		err_code = nc_get_att_text(ncID, containerVarID, CF_SG_NODE_COUNT, node_count_name);

		// Detect if variable already exists in dataset. If it doesn't, define it
		int varId;
		int ncount_err_code = nc_inq_varid(ncID, node_count_name, &varId);

		if (ncount_err_code != NC_NOERR)
		{
			// error
		}


		// Append from the end
		int ncount_add = static_cast<int>(ft.getTotalNodeCount());
		size_t ind[1] = { next_write_pos_node_count };
		err_code = nc_put_var1_int(ncID, varId, ind, &ncount_add);
		next_write_pos_node_count++;

		// Write each point from each part in node coordinates
		for(size_t part_no = 0; part_no < ft.getTotalPartCount(); part_no++)
		{
			if(( ft.getType() == POLYGON && this->interiorRingDetected ) || ft.getType() == MULTILINE || ft.getType() == MULTIPOLYGON)
			{
				// if interior rings is present, go write part node counts and interior ring info
				if( (ft.getType() == POLYGON || ft.getType() == MULTIPOLYGON) && this->interiorRingDetected )
				{
					int interior_ring_fl = 0;
					
					if(ft.getType() == POLYGON)
						interior_ring_fl = part_no == 0 ? 0 : 1;
					if(ft.getType() == MULTIPOLYGON)
						ft.IsPartAtIndInteriorRing(static_cast<int>(part_no)) ? 1 : 0; 
					nc_put_var1_int(ncID, intring_varID, &next_write_pos_pnc, &interior_ring_fl);
				}

				int pnc_writable = ft.getPerPartNodeCount()[part_no];
				int err_code = nc_put_var1_int(ncID, pnc_varID, &next_write_pos_pnc, &pnc_writable);
				next_write_pos_pnc++;
			}

			for(size_t pt_ind = 0; pt_ind < ft.getPerPartNodeCount()[part_no]; pt_ind++)
			{
				int pt_ind_int = static_cast<int>(pt_ind);
				OGRPoint& write_pt = ft.getPoint(part_no, pt_ind_int);
			
				// Write each node coordinate
				double x = write_pt.getX();
				nc_put_var1_double(ncID, node_coordinates_varIDs[0], &next_write_pos_node_coord, &x);
				double y = write_pt.getY();
				nc_put_var1_double(ncID, node_coordinates_varIDs[1], &next_write_pos_node_coord, &y);
				if(this->node_coordinates_varIDs.size() > 2)
				{
					double z = write_pt.getZ();
					nc_put_var1_double(ncID, node_coordinates_varIDs[2], &next_write_pos_node_coord, &z);
				}

				// Step the position
				this->next_write_pos_node_coord++;		
			}
		}
	}

	OGR_SGeometry_Scribe::OGR_SGeometry_Scribe() :
		ncID(0),
		containerVarID(INVALID_VAR_ID),
		interiorRingDetected(false),
		node_coordinates_dimID(INVALID_DIM_ID),
		node_count_dimID(INVALID_DIM_ID),
		node_count_varID(INVALID_VAR_ID),
		pnc_varID(INVALID_VAR_ID),
		intring_varID(INVALID_VAR_ID),
		next_write_pos_node_coord(0),
		next_write_pos_node_count(0),
		next_write_pos_pnc(0)
	{

	}

	OGR_SGeometry_Scribe::OGR_SGeometry_Scribe(int ncID_in, int container_varID_in)
		: ncID(ncID_in),
		containerVarID(container_varID_in),
		interiorRingDetected(false),
		next_write_pos_node_coord(0),
		next_write_pos_node_count(0),
		next_write_pos_pnc(0)
	{
		char container_name[NC_MAX_CHAR + 1];
		memset(container_name, 0, NC_MAX_CHAR + 1);

		// Prepare variable names
		char node_coord_names[NC_MAX_CHAR + 1];
		memset(node_coord_names, 0, NC_MAX_CHAR + 1);

		char node_count_name[NC_MAX_CHAR + 1];
		memset(node_count_name, 0, NC_MAX_CHAR + 1);

		// Set default values
		pnc_varID = INVALID_VAR_ID;
		pnc_dimID = INVALID_DIM_ID;
		intring_varID = INVALID_VAR_ID;

		int err_code;
		err_code = nc_get_att_text(ncID, containerVarID, CF_SG_NODE_COORDINATES, node_coord_names);
		err_code = nc_get_att_text(ncID, containerVarID, CF_SG_NODE_COUNT, node_count_name);
		err_code = nc_inq_varname(ncID, containerVarID, container_name);

		// Make dimensions for each of these
		err_code = nc_def_dim(ncID_in, node_count_name, 1, &node_count_dimID);
		err_code = nc_def_dim(ncID_in, container_name, 1, &node_coordinates_dimID);

		// Define variables for each of those
		err_code = nc_def_var(ncID_in, node_count_name, NC_INT, 1, &node_count_dimID, &node_count_varID);

		// Do the same for part node count, if it exists
		char pnc_name[NC_MAX_CHAR + 1];
		memset(pnc_name, 0, NC_MAX_CHAR + 1);
		err_code = nc_get_att_text(ncID, containerVarID, CF_SG_PART_NODE_COUNT, pnc_name);
		if(err_code == NC_NOERR)
		{
			err_code = nc_def_dim(ncID_in, pnc_name, 1, &pnc_dimID);
			err_code = nc_def_var(ncID_in, pnc_name, NC_INT, 1, &pnc_dimID, &pnc_varID);
		}

		// Node coordinates
		int new_varID;
		char * pszNcoord = strtok(node_coord_names, " ");
		int count = 0;

		while (pszNcoord != nullptr)
		{
			// Define a new variable with that name
			err_code = nc_def_var(ncID, pszNcoord, NC_DOUBLE, 1, &node_coordinates_dimID, &new_varID);

			// to do: check for error

			// Add it to the coordinate varID list
			this->node_coordinates_varIDs.push_back(new_varID);

			// Add the mandatory "axis" attribute
			switch(count)
			{
				case 0:
					// first it's X
					nc_put_att_text(ncID, new_varID, CF_AXIS, strlen(CF_SG_X_AXIS), CF_SG_X_AXIS);
					break;
				case 1:
					// second it's Y
					nc_put_att_text(ncID, new_varID, CF_AXIS, strlen(CF_SG_Y_AXIS), CF_SG_Y_AXIS);
					break;
				case 2: 
					// third it's Z
					nc_put_att_text(ncID, new_varID, CF_AXIS, strlen(CF_SG_Z_AXIS), CF_SG_Z_AXIS);
					break;
			}	

			pszNcoord = strtok(nullptr, " ");
			count++;
		}
	}
	
	void OGR_SGeometry_Scribe::redef_interior_ring()
	{
		
		char container_name[NC_MAX_NAME + 1];
		memset(container_name, 0, NC_MAX_NAME + 1);

		nc_redef(ncID);
		nc_inq_varname(ncID, containerVarID, container_name);			
		std::string int_ring = std::string(container_name) + std::string("_interior_ring");
		std::string pnc_name = std::string(container_name) + std::string("_part_node_count");

		// Put the new interior ring attribute
		nc_put_att_text(ncID, containerVarID, CF_SG_INTERIOR_RING, strlen(int_ring.c_str()), int_ring.c_str()); 
		size_t pnc_dim_len = 0;

		nc_inq_dimlen(ncID, pnc_dimID, &pnc_dim_len);

		// Define the new variable
		nc_def_var(ncID, int_ring.c_str(), NC_INT, 1, &pnc_dimID, &intring_varID);
		nc_enddef(ncID);

		const int zero_fill = 0;

		// Zero fill interior ring
		for(size_t itr = 0; itr < pnc_dim_len; itr++)
		{
			nc_put_var1_int(ncID, intring_varID, &itr, &zero_fill);
		}
	}

	void OGR_SGeometry_Scribe::redef_pnc()
	{
		char container_name[NC_MAX_NAME + 1];
		memset(container_name, 0, NC_MAX_NAME + 1);

		nc_redef(ncID);
		nc_inq_varname(ncID, containerVarID, container_name);			
		std::string pnc_name = std::string(container_name) + std::string("_part_node_count");

		// Put the new interior ring attribute
		nc_put_att_text(ncID, containerVarID, CF_SG_PART_NODE_COUNT, strlen(pnc_name.c_str()), pnc_name.c_str()); 

		// If the PNC dim doesn't exist, define it

		size_t pnc_dim_len = 0;

		if(pnc_dimID == INVALID_DIM_ID)
		{
			// Initialize it with size of node_count Dim
			size_t ncount_len;
			nc_inq_dimlen(ncID, node_count_dimID, &ncount_len);
			nc_def_dim(ncID, pnc_name.c_str(), ncount_len, &pnc_dimID);
		}
		
		nc_inq_dimlen(ncID, pnc_dimID, &pnc_dim_len);

		// Define the new variable
		nc_def_var(ncID, pnc_name.c_str(), NC_INT, 1, &pnc_dimID, &pnc_varID);
		nc_enddef(ncID);

		// Fill pnc with the current values of node counts
		for(size_t itr = 0; itr < pnc_dim_len; itr++)
		{
			int ncount_in;
			nc_get_var1_int(ncID, node_count_varID, &itr, &ncount_in);
			nc_put_var1_int(ncID, pnc_varID, &itr, &ncount_in);
		}
	}

	int write_Geometry_Container
		(int ncID, std::string name, geom_t geometry_type, std::vector<std::string> & node_coordinate_names)
	{

		int write_var_id;
		int err_code;
		
		// Define geometry container variable
		err_code = nc_def_var(ncID, name.c_str(), NC_FLOAT, 0, nullptr, &write_var_id);
		// todo: exception handling of err_code

		
		/* Geometry Type Attribute
		 * -
		 */

		// Next, go on to add attributes needed for each geometry type
		std::string geometry_str =
			(geometry_type == POINT || geometry_type == MULTIPOINT) ? CF_SG_TYPE_POINT :
			(geometry_type == LINE || geometry_type == MULTILINE) ? CF_SG_TYPE_LINE :
			(geometry_type == POLYGON || geometry_type == MULTIPOLYGON) ? CF_SG_TYPE_POLY :
			""; // obviously an error condition...

		// todo: error on "none"

		// Add the geometry type attribute
		err_code = nc_put_att_text(ncID, write_var_id, CF_SG_GEOMETRY_TYPE, geometry_str.size(), geometry_str.c_str());

		/* Node Coordinates Attribute
		 * -
		 */
		std::string ncoords_atr_str = "";

		for(size_t itr = 0; itr < node_coordinate_names.size(); itr++)
		{
			ncoords_atr_str += node_coordinate_names[itr];
			if (itr < node_coordinate_names.size() - 1)
			{
				ncoords_atr_str += " ";
			}
		}

		err_code = nc_put_att_text(ncID, write_var_id, CF_SG_NODE_COORDINATES, ncoords_atr_str.size(), ncoords_atr_str.c_str());

		// The previous two attributes are all that are required from POINT


		/* Node_Count Attribute
		 * (not needed for POINT)
		 */
		if (geometry_type != POINT)
		{
			std::string nodecount_atr_str = name + "_node_count";
			
			err_code = nc_put_att_text(ncID, write_var_id, CF_SG_NODE_COUNT, nodecount_atr_str.size(), nodecount_atr_str.c_str());
		}

		/* Part_Node_Count Attribute
		 * (only needed for MULTILINE, MULTIPOLYGON, and (potentially) POLYGON)
		 */
		if (geometry_type == MULTILINE || geometry_type == MULTIPOLYGON)
		{
			std::string pnc_atr_str = name + "_part_node_count";

			err_code = nc_put_att_text(ncID, write_var_id, CF_SG_PART_NODE_COUNT, pnc_atr_str.size(), pnc_atr_str.c_str());
		}

		return write_var_id;
	}

	void nc_write_x_y_CF_axis(int ncID, int Xaxis_ID, int Yaxis_ID)
	{
		int err;
		err = nc_put_att_text(ncID, Xaxis_ID, CF_AXIS, strlen(CF_SG_X_AXIS), CF_SG_X_AXIS);
		err = nc_put_att_text(ncID, Yaxis_ID, CF_AXIS, strlen(CF_SG_Y_AXIS), CF_SG_Y_AXIS);

		// to do: throw excepton
	}
}
