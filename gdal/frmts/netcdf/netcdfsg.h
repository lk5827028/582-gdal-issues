#ifndef __NETCDFSG_H__
#define __NETCDFSG_H__
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Interface used for netCDF functions
// implementing awareness for the CF-1.8 convention
//
// Author: wchen329
namespace nccfdriver
{	
	// Enum used for easily identifying Geometry types
	enum geom_t
	{
		NONE,		// no geometry found
		POLYGON,	// OGRPolygon
		MULTIPOLYGON,	// OGRMultipolygon
		LINE,		// OGRLineString
		MULTILINE,	// OGRMultiLineString
		POINT,		// OGRPoint
		MULTIPOINT	// OGRMultiPoint
	};	

	// Concrete "Point" class, holds n dimensional double precision floating point value, defaults to all zero values
	class Point
	{
		int size;
		double * values;	
		Point(Point &);
		Point operator=(const Point &);

		public:
		~Point();	
		Point(int dim = 0) : size(dim)
			{ this->values = new double[dim]; } // memset(this->values, 0, dim*sizeof(double)); }	
		double& operator[](int i) { return this->values[i]; }
		int getOrder() { return this->size; }
	};



	// Simple geometry - doesn't actually hold the points, rather serves
	// as a pseudo reference to a NC variable
	class SGeometry
	{
		char * container_name;	// name of the underlying geometry container
		geom_t type;	 	// internal geometry type structure
		int ncid;		// ncid - as used in netcdf.h
		int gc_varId;		// the id of the underlying geometry_container variable
		int touple_order;	// amount of "coordinates" in a point
		std::vector<int> nodec_varIds;	// varIds for each node_coordinate entry
		std::vector<int> node_counts;	// node counts of each geometry in a container
		std::vector<int> pnode_counts;	// part node counts of each geometry in a container
		std::vector<int> bound_list;	// a quick list used to store the real beginning indicies of shapes
		std::vector<bool> int_rings;	// list of parts that are interior rings
		std::vector<int> pnc_bl;	// a quick list of indicies for part counts corresponding to a geometry
		std::vector<int> parts_count;	// a count of total parts in a single geometry instance
		std::vector<int> poly_count;	// count of polygons, for use only when interior rings are present
		int current_vert_ind;	// used to keep track of current point being used
		size_t cur_geometry_ind;	// used to keep track of current geometry index
		size_t cur_part_ind;		// used to keep track of current part index
		bool interior;		// interior ring = true. only meaningful for polygons
		bool valid;		// true if geometry is valid, or false if construction failed, improve by using exception
		Point * pt_buffer;	// holds the current point
		SGeometry(SGeometry &);
		SGeometry operator=(const SGeometry &);

		public:

		/* Point* SGeometry::next_pt()
		 * returns a pointer to the next pt in sequence, if any. If none, returns a nullptr
		 * calling next_pt does not have additional space requirements
		 */
		Point* next_pt(); 
		bool has_next_pt(); // returns whether or not the geometry has another point
			
		/* void SGeometry::next_geometry()
		 * does not return anything. Rather, the SGeometry for which next_geometry() was called
		 * essentially gets replaced by the new geometry. So to access the next geometry,
		 * no additional space is required, just use the reference to the previous geometry, now
		 * pointing to the new geometry.
		 */
		void next_geometry(); // simply reuses the host structure
		bool has_next_geometry();

		/* int SGeometry::get_part_num()
		 * Retrieves the corresponding part number of the part within a geometry that the next_pt() belongs to. 
		 * Part number meaning the number of that part within a geometry
		 * If a geometry is single part, or the variable explicitly contains no multipart geometries, then it is "1"
		 */
		int get_part_num();

		/* bool SGeometry::is_interior()
		 * Retrieves whether or not the point to be returned through next_pt() is a part of
		 * an interior ring structure. If the structure has no interior rings, then false by default
		 */
		bool is_interior();

		/* geom_t getGeometryType()
		 * Retrieves the associated geometry type with this geometry
		 */
		geom_t getGeometryType() { return this->type; }

		/* void SGeometry::get_geometry_count()
		 * returns a size, indicating the amount of geometries
		 * contained in the variable
		 */
		size_t get_geometry_count();

		/* bool SGeometry::getValid()
		 * Gets the valid flag of this geometry.
		 * Future work: replace by exception handling
		 */
		bool getValid() { return this->valid; }

		int getContainerId() { return gc_varId; }

		/* void * serializeToWKB
		 * Returns a pre-allocated array which serves as the WKB reference to this geometry
		 * the size of the WKB representation is written to the passed in wkbSize
		 */
		void * serializeToWKB(int featureInd, size_t& wkbSize);

		/* Return a point at a specific index specifically
		 * this point should NOT be explicitly freed.
		 *
		 */
		Point* operator[](int ind);

		/* ncID - as used in netcdf.h
		 * baseVarId - the id of a variable with a geometry container attribute 
		 */
		SGeometry(int ncId, int baseVarId); 
		~SGeometry();
	};

	/* SGeometry_PropertyReader
	 * Holds properties for geometry containers
	 * Pass in the geometry_container ID, automatically scans the netcdf Dataset for properties associated
	 *
	 * to construct: pass in the ncid which the reader should work over
	 */
	class SGeometry_PropertyReader
	{
		std::map<int, std::vector<std::pair<int, std::string>>> m; // key: varID of geometry container, value: a vector of: {varId of property, name}
		int max_seek;
		int nc;
		
		public:
			void open(int container_id);	// opens and intializes a geometry_container into the map
			std::vector<std::pair<std::string, std::string>> fetch(int cont_lookup, size_t seek_pos);
							// returns for each property {Property_Name, Property_Value} at a certain position
			std::vector<std::string> headers(int cont_lookup);
			std::vector<int> ids(int cont_lookup);
			SGeometry_PropertyReader(int ncid) : max_seek(0), nc(ncid) {}
	};

	// General exception interface for Simple Geometries
	// Whatever pointer returned should NOT be freed- it will be deconstructed automatically, if needed
	class SG_Exception
	{
		public:
			virtual char * get_err_msg() = 0;	
			virtual ~SG_Exception();
	};

	// Mismatched dimension exception
	class SG_Exception_Dim_MM : public SG_Exception
	{
		char * err_msg;	
		SG_Exception_Dim_MM(SG_Exception_Dim_MM&);
		SG_Exception_Dim_MM& operator=(SG_Exception_Dim_MM&);

		public:
			char* get_err_msg() override { return err_msg; }

		SG_Exception_Dim_MM(char* geometry_container, const char* field_1, const char *field_2);
		~SG_Exception_Dim_MM(){ delete[] err_msg; }
	};

	// Missing (existential) property error
	class SG_Exception_Existential: public SG_Exception
	{
		char * err_msg;
		SG_Exception_Existential(SG_Exception_Existential&);
		SG_Exception_Existential& operator=(SG_Exception_Existential&);

		public:
			char* get_err_msg() override { return err_msg; }
		
		SG_Exception_Existential(char* geometry_container, const char* missing_name);
		~SG_Exception_Existential(){ delete[] err_msg; }
	};

	// Missing dependent property (arg_1 is dependent on arg_2)
	class SG_Exception_Dep: public SG_Exception
	{
		char * err_msg;
		SG_Exception_Dep(SG_Exception_Dep&);
		SG_Exception_Dep& operator=(SG_Exception_Dep&);

		public:
			char* get_err_msg() override { return err_msg; }
		
		SG_Exception_Dep(char* geometry_container, const char* arg_1, const char* arg_2);
		~SG_Exception_Dep(){ delete[] err_msg; }
	};

	// The sum of all values in a variable does not match the sum of another variable
	class SG_Exception_BadSum : public SG_Exception
	{
		char * err_msg;
		SG_Exception_BadSum(SG_Exception_BadSum&);
		SG_Exception_BadSum& operator=(SG_Exception_BadSum&);

		public:
			char* get_err_msg() override { return err_msg; }
		
		SG_Exception_BadSum(char* geometry_container, const char* arg_1, const char* arg_2);
		~SG_Exception_BadSum(){ delete[] err_msg; }
	};

	// Unsupported Feature Type
	class SG_Exception_BadFeature : public SG_Exception
	{
		const char * err_msg;

		public:
			char* get_err_msg() override { return (char*)err_msg; }
		
		SG_Exception_BadFeature() : err_msg("Unsupported or unrecognized feature type.") {}
	};

	// Some helpers which simply call some netcdf library functions, unless otherwise mentioned, ncid, refers to its use in netcdf.h
	
	/* Retrieves the minor version from the value Conventions global attr
	 * Returns: a positive integer corresponding to the conventions value
	 *	if not CF-1.x then return negative value, -1
	 */
	int getCFMinorVersion(int ncid);

	/* Given a geometry_container varID, searches that variable for a geometry_type attribute
	 * Returns: the equivalent geometry type
	 */
	geom_t getGeometryType(int ncid, int varid); 
	
	void* inPlaceSerialize_Point(SGeometry * ge, int seek_pos, void * serializeBegin);
	void* inPlaceSerialize_LineString(SGeometry * ge, int node_count, int seek_begin, void * serializeBegin);
	void* inPlaceSerialize_PolygonExtOnly(SGeometry * ge, int node_count, int seek_begin, void * serializeBegin);
	void* inPlaceSerialize_Polygon(SGeometry * ge, std::vector<int>& pnc, int ring_count, int seek_begin, void * serializeBegin);

	/* scanForGeometryContainers
	 * A simple function that scans a netCDF File for Geometry Containers
	 * -
	 * Scans the given ncid for geometry containers
	 * The vector passed in will be overwritten with a vector of scan results
	 */
	int scanForGeometryContainers(int ncid, std::vector<int> & r_ids);

	/* Given a variable name, and the ncid, returns a SGeometry reference object, which acts more of an iterator of a geometry container
	 *
	 * Returns: a NEW geometry "reference" object, that can be used to quickly access information about the object, nullptr if invalid or an error occurs
	 */
	SGeometry* getGeometryRef(int ncid, const char * varName );	

	/* Writes a geometry to the specified NC file.
	 * The write uses the following guidelines
	 * - the geometry variable name is geometryX where X is some natural number
	 * - the geometry container is called geometry_containerX, corresponding to some geometryX
	 *
	 * Returns: an error code if failure, 0 on success
	 */
	//int putGeometryRef(int ncid, SGeometry* geometry);

	/* Fetches a one dimensional string attribute
	 * using the given ncid, variable ID (varID), and attribute key (attrName)
	 *
	 * Returns: a NEW cstring of the attribute value, that must be separately deleted 
	 */
	char * attrf(int ncid, int varId, const char * attrName);
}

#endif
