#include "gdal_map_algebra_private.h"

template<typename datatype1,typename datatype2> struct gma_two_bands_callback {
    typedef int (*type)(gma_band<datatype1>, gma_block*, gma_band<datatype2>, gma_object_t**, gma_object_t*);
    type fct;
};

template<typename type1,typename type2>
type1 gma_test_operator(gma_logical_operation_p<type2> *op, type2 value) {
    switch (op->m_op) {
    case gma_eq:
        return value == op->m_value;
    case gma_ne:
        return value != op->m_value;
    case gma_gt:
        return value > op->m_value;
    case gma_lt:
        return value < op->m_value;
    case gma_ge:
        return value >= op->m_value;
    case gma_le:
        return value <= op->m_value;
    case gma_and:
        return value && op->m_value;
    case gma_or:
        return value || op->m_value;
    case gma_not:
        return not value;
    }
}

template<typename type1,typename type2>
int gma_assign_band(gma_band<type1> band1, gma_block *block1, gma_band<type2> band2, gma_object_t**, gma_object_t *arg) {
    // arg is checked here
    gma_cell_index i1;
    for (i1.y = 0; i1.y < block1->h; i1.y++) {
        for (i1.x = 0; i1.x < block1->w; i1.x++) {
            if (gma_is_nodata(type1,band1,block1,i1)) continue;
            type2 value;
            if (gma_value_from_other_band<type1,type2>(band1, block1, i1, band2, &value)) {
                if (gma_is_nodata_value(type2,band2,value)) continue;
                if (arg) {
                    if (gma_test_operator<type1,type2>((gma_logical_operation_p<type2> *)arg, value))
                        gma_block_cell(type1, block1, i1) = value;
                } else
                    gma_block_cell(type1, block1, i1) = value;
            }
        }
    }
    return 2;
}

template<typename type1,typename type2>
int gma_add_band(gma_band<type1> band1, gma_block *block1, gma_band<type2> band2, gma_object_t**, gma_object_t *arg) {
    gma_cell_index i1;
    for (i1.y = 0; i1.y < block1->h; i1.y++) {
        for (i1.x = 0; i1.x < block1->w; i1.x++) {
            if (gma_is_nodata(type1,band1,block1,i1)) continue;
            type2 value;
            if (gma_value_from_other_band<type1,type2>(band1, block1, i1, band2, &value)) {
                if (arg) {
                    if (gma_test_operator<type1,type2>((gma_logical_operation_p<type2> *)arg, value))
                        gma_block_cell(type1, block1, i1) += value;
                } else
                    gma_block_cell(type1, block1, i1) += value;
            }
        }
    }
    return 2;
}

template<typename type1,typename type2>
int gma_subtract_band(gma_band<type1> band1, gma_block *block1, gma_band<type2> band2, gma_object_t**, gma_object_t *arg) {
    gma_cell_index i1;
    for (i1.y = 0; i1.y < block1->h; i1.y++) {
        for (i1.x = 0; i1.x < block1->w; i1.x++) {
            if (gma_is_nodata(type1,band1,block1,i1)) continue;
            type2 value;
            if (gma_value_from_other_band<type1,type2>(band1, block1, i1, band2, &value)) {
                if (arg) {
                    if (gma_test_operator<type1,type2>((gma_logical_operation_p<type2> *)arg, value))
                        gma_block_cell(type1, block1, i1) -= value;
                } else
                    gma_block_cell(type1, block1, i1) -= value;
            }
        }
    }
    return 2;
}

template<typename type1,typename type2>
int gma_multiply_by_band(gma_band<type1> band1, gma_block *block1, gma_band<type2> band2, gma_object_t**, gma_object_t *arg) {
    gma_cell_index i1;
    for (i1.y = 0; i1.y < block1->h; i1.y++) {
        for (i1.x = 0; i1.x < block1->w; i1.x++) {
            if (gma_is_nodata(type1,band1,block1,i1)) continue;
            type2 value;
            if (gma_value_from_other_band<type1,type2>(band1, block1, i1, band2, &value)) {
                if (arg) {
                    if (gma_test_operator<type1,type2>((gma_logical_operation_p<type2> *)arg, value))
                        gma_block_cell(type1, block1, i1) *= value;
                } else
                    gma_block_cell(type1, block1, i1) *= value;
            }
        }
    }
    return 2;
}

template<typename type1,typename type2>
int gma_divide_by_band(gma_band<type1> band1, gma_block *block1, gma_band<type2> band2, gma_object_t**, gma_object_t *arg) {
    gma_cell_index i1;
    for (i1.y = 0; i1.y < block1->h; i1.y++) {
        for (i1.x = 0; i1.x < block1->w; i1.x++) {
            if (gma_is_nodata(type1,band1,block1,i1)) continue;
            type2 value;
            if (gma_value_from_other_band<type1,type2>(band1, block1, i1, band2, &value)) {
                if (arg) {
                    if (gma_test_operator<type1,type2>((gma_logical_operation_p<type2> *)arg, value))
                        gma_block_cell(type1, block1, i1) /= value;
                } else
                    gma_block_cell(type1, block1, i1) /= value;
            }
        }
    }
    return 2;
}

template<typename type1,typename type2>
int gma_modulus_by_band(gma_band<type1> band1, gma_block *block1, gma_band<type2> band2, gma_object_t**, gma_object_t *arg) {
    gma_cell_index i1;
    for (i1.y = 0; i1.y < block1->h; i1.y++) {
        for (i1.x = 0; i1.x < block1->w; i1.x++) {
            if (gma_is_nodata(type1,band1,block1,i1)) continue;
            type2 value;
            if (gma_value_from_other_band<type1,type2>(band1, block1, i1, band2, &value)) {
                if (arg) {
                    if (gma_test_operator<type1,type2>((gma_logical_operation_p<type2> *)arg, value))
                        gma_block_cell(type1, block1, i1) %= value;
                } else
                    gma_block_cell(type1, block1, i1) %= value;
            }
        }
    }
    return 2;
}

#define gma_modulus_by_band_type_error(type1,type2)                     \
    template<>                                                          \
    int gma_modulus_by_band<type1,type2>(gma_band<type1>, gma_block*, gma_band<type2>, gma_object_t**, gma_object_t*) { \
        fprintf(stderr, "invalid type to binary operator %%");          \
        return 0;                                                       \
    }

gma_modulus_by_band_type_error(uint8_t,float)
gma_modulus_by_band_type_error(uint8_t,double)
gma_modulus_by_band_type_error(float,uint8_t)
gma_modulus_by_band_type_error(double,uint8_t)
gma_modulus_by_band_type_error(uint16_t,float)
gma_modulus_by_band_type_error(uint16_t,double)
gma_modulus_by_band_type_error(float,uint16_t)
gma_modulus_by_band_type_error(double,uint16_t)
gma_modulus_by_band_type_error(int16_t,float)
gma_modulus_by_band_type_error(int16_t,double)
gma_modulus_by_band_type_error(float,int16_t)
gma_modulus_by_band_type_error(double,int16_t)
gma_modulus_by_band_type_error(uint32_t,float)
gma_modulus_by_band_type_error(uint32_t,double)
gma_modulus_by_band_type_error(float,uint32_t)
gma_modulus_by_band_type_error(double,uint32_t)
gma_modulus_by_band_type_error(int32_t,float)
gma_modulus_by_band_type_error(int32_t,double)
gma_modulus_by_band_type_error(float,int32_t)
gma_modulus_by_band_type_error(double,int32_t)
gma_modulus_by_band_type_error(float,float)
gma_modulus_by_band_type_error(float,double)
gma_modulus_by_band_type_error(double,float)
gma_modulus_by_band_type_error(double,double)

// zonal min should be gma_hash_p<gma_int> or gma_hash_p<gma_float> depending on the datatype

template<typename zones_type,typename values_type>
int gma_zonal_min(gma_band<zones_type> zones_band, gma_block *zones_block, gma_band<values_type> values_band, gma_object_t **retval, gma_object_t*) {
    gma_retval_init(gma_hash_p<zones_type COMMA gma_number_p<values_type> >, rv, );
    gma_cell_index zones_i;
    for (zones_i.y = 0; zones_i.y < zones_block->h; zones_i.y++) {
        for (zones_i.x = 0; zones_i.x < zones_block->w; zones_i.x++) {
            if (gma_is_nodata(zones_type,zones_band,zones_block,zones_i)) continue;
            values_type value;
            gma_value_from_other_band<zones_type,values_type>(zones_band, zones_block, zones_i, values_band, &value);
            zones_type zone = gma_block_cell(zones_type, zones_block, zones_i);
            if (!zone)
                continue;
            if (rv->exists(zone)) {
                values_type old_value = rv->get(zone)->value();
                if (value > old_value)
                    continue;
            }
            rv->put(zone, new gma_number_p<values_type>(value));
        }
    }
    return 1;
}

template<typename zones_type,typename values_type>
int gma_zonal_max(gma_band<zones_type> zones_band, gma_block *zones_block, gma_band<values_type> values_band, gma_object_t **retval, gma_object_t*) {
    gma_retval_init(gma_hash_p<zones_type COMMA gma_number_p<values_type> >, rv, );
    gma_cell_index zones_i;
    for (zones_i.y = 0; zones_i.y < zones_block->h; zones_i.y++) {
        for (zones_i.x = 0; zones_i.x < zones_block->w; zones_i.x++) {
            if (gma_is_nodata(zones_type,zones_band,zones_block,zones_i)) continue;
            values_type value;
            gma_value_from_other_band<zones_type,values_type>(zones_band, zones_block, zones_i, values_band, &value);
            zones_type zone = gma_block_cell(zones_type, zones_block, zones_i);
            if (!zone)
                continue;
            if (rv->exists(zone)) {
                values_type old_value = ((gma_number_p<values_type>*)rv->get(zone))->value();
                if (value < old_value)
                    continue;
            }
            rv->put(zone, new gma_number_p<values_type>(value));
        }
    }
    return 1;
}

template<typename rims_type,typename areas_type>
int gma_rim_by8(gma_band<rims_type> rims_band, gma_block *rims_block, gma_band<areas_type> areas_band, gma_object_t**, gma_object_t*) {
    gma_cell_index i;
    for (i.y = 0; i.y < rims_block->h; i.y++) {
        for (i.x = 0; i.x < rims_block->w; i.x++) {

            // if the 8-neighborhood in areas is all of the same area, then set rims = 0, otherwise from area

            areas_type area;
            gma_value_from_other_band<rims_type,areas_type>(rims_band, rims_block, i, areas_band,  &area);

            rims_type my_area = 0;

            gma_cell_index in = gma_cell_first_neighbor(i);
            for (int neighbor = 1; neighbor < 9; neighbor++) {
                gma_cell_move_to_neighbor(in, neighbor);
                areas_type n_area;
                int has_neighbor = gma_value_from_other_band<rims_type,areas_type>(rims_band, rims_block, in, areas_band,  &n_area);
                if (!has_neighbor || (has_neighbor && (n_area != area))) {
                    my_area = area;
                    break;
                }
            }

            gma_block_cell(rims_type, rims_block, i) = my_area;

        }
    }
    return 2;
}

// The D8 directions method, compute direction to lowest 8-neighbor
//
// neighbors:
// 8 1 2
// 7 x 3
// 6 5 4
//
// case of nothing lower => flat = pseudo direction 10
// case of all higher => pit = pseudo direction 0
//
// if we are on global border and the cell is flat or pit,
// then set direction to out of the map
//
// todo: no data cells, mask?
// currently if two neighbors are equally lower, the first is picked
//
template<typename fd_t, typename dem_t>
int gma_D8(gma_band<fd_t> band_fd, gma_block *block_fd, gma_band<dem_t> band_dem, gma_object_t**, gma_object_t*) {
    int border_block = is_border_block(band_fd, block_fd);
    gma_cell_index i_fd;
    for (i_fd.y = 0; i_fd.y < block_fd->h; i_fd.y++) {
        for (i_fd.x = 0; i_fd.x < block_fd->w; i_fd.x++) {
            int border_cell = is_border_cell(block_fd, border_block, i_fd);

            dem_t my_elevation;
            gma_value_from_other_band<fd_t,dem_t>(band_fd, block_fd, i_fd, band_dem,  &my_elevation);

            dem_t lowest;
            int dir;
            int first = 1;

            gma_cell_index i_n_fd = gma_cell_first_neighbor(i_fd);
            for (int neighbor = 1; neighbor < 9; neighbor++) {
                gma_cell_move_to_neighbor(i_n_fd, neighbor);

                dem_t tmp;
                if (!gma_value_from_other_band<fd_t,dem_t>(band_fd, block_fd, i_n_fd, band_dem, &tmp))
                    continue;

                if (first || tmp < lowest) {
                    first = 0;
                    lowest = tmp;
                    dir = neighbor;
                }

            }

            // is this flat area or a pit?
            if (first || lowest > my_elevation)
                dir = 0;
            else if (lowest == my_elevation)
                dir = 10;

            if (border_cell && (dir == 0 || dir == 10))
                dir = border_cell;

            gma_block_cell(fd_t, block_fd, i_fd) = dir;

        }
    }
    return 2;
}

class gma_band_iterator_t : public gma_object_t {
public:
    long count_in_this_loop_of_band;
    long total_count;
    gma_band_iterator_t() {
        count_in_this_loop_of_band = 0;
        total_count = 0;
    }
    void new_loop() {
        count_in_this_loop_of_band = 0;
    }
    void add() {
        count_in_this_loop_of_band++;
        total_count++;
    }
};

// drain flat cells (10) to neighboring non-flat cells which are at same or lower elevation
// this leaves low lying flat areas undrained
template<typename fd_t, typename dem_t>
int gma_route_flats(gma_band<fd_t> band_fd, gma_block *block_fd, gma_band<dem_t> band_dem, gma_object_t **retval, gma_object_t*) {
    gma_retval_init(gma_band_iterator_t, rv, );
    if (gma_first_block(block_fd))
        rv->new_loop();
    gma_cell_index i_fd;
    for (i_fd.y = 0; i_fd.y < block_fd->h; i_fd.y++) {
        for (i_fd.x = 0; i_fd.x < block_fd->w; i_fd.x++) {

            // if not flat cell, nothing to do
            if (gma_block_cell(fd_t, block_fd, i_fd) != 10) continue;

            dem_t my_elevation;
            gma_value_from_other_band<fd_t,dem_t>(band_fd, block_fd, i_fd, band_dem,  &my_elevation);

            fd_t new_dir = 0;
            gma_cell_index in_fd = gma_cell_first_neighbor(i_fd);
            for (int neighbor = 1; neighbor < 9; neighbor++) {
                gma_cell_move_to_neighbor(in_fd, neighbor);

                fd_t n_dir;
                if (!gma_value_from_other_band<fd_t>(band_fd, block_fd, in_fd, band_fd,  &n_dir))
                    continue;  // we are at border and this is outside

                if (n_dir == 10) continue;

                dem_t n_elevation;
                gma_value_from_other_band<fd_t,dem_t>(band_fd, block_fd, in_fd, band_dem,  &n_elevation);

                if (n_elevation > my_elevation) continue;

                new_dir = neighbor;
                break;
            }
            if (new_dir == 0) continue;

            gma_block_cell(fd_t, block_fd, i_fd) = new_dir;
            rv->add();

        }
    }

    if (gma_last_block(band_fd, block_fd))
        fprintf(stderr, "%ld flat cells routed.\n", rv->count_in_this_loop_of_band);

    if (rv->count_in_this_loop_of_band)
        return 4;
    else
        return 2;
}

template<typename filled_t, typename dem_t>
int gma_fill_depressions(gma_band<filled_t> filled_band, gma_block *filled_block, gma_band<dem_t> dem_band, gma_object_t **retval, gma_object_t*) {
    gma_retval_init(gma_band_iterator_t, rv, );
    if (gma_first_block(filled_block))
        rv->new_loop();
    int border_block = is_border_block(filled_band, filled_block);
    gma_cell_index i;
    for (i.y = 0; i.y < filled_block->h; i.y++) {
        for (i.x = 0; i.x < filled_block->w; i.x++) {
            int border_cell = is_border_cell(filled_block, border_block, i);
            dem_t dem_e;
            gma_value_from_other_band<filled_t,dem_t>(filled_band, filled_block, i, dem_band, &dem_e);

            // initially my_e is set to max e of dem
            // set my_e = max(dem_e, lowest_e_in_nhood)

            filled_t new_e = dem_e, lowest_e_in_nhood;
            if (border_cell)
                lowest_e_in_nhood = 0;
            else {
                int f = 1;
                gma_cell_index in = gma_cell_first_neighbor(i);
                for (int neighbor = 1; neighbor < 9; neighbor++) {
                    gma_cell_move_to_neighbor(in, neighbor);
                    filled_t n_e;
                    gma_value_from_other_band<filled_t,filled_t>(filled_band, filled_block, in, filled_band, &n_e);
                    if (f || n_e < lowest_e_in_nhood) {
                        f = 0;
                        lowest_e_in_nhood = n_e;
                    }
                }
            }
            if (lowest_e_in_nhood > new_e)
                new_e = lowest_e_in_nhood;

            filled_t old_e = gma_block_cell(filled_t, filled_block, i);
            if (new_e < old_e) {
                gma_block_cell(filled_t, filled_block, i) = new_e;
                rv->add();
            }

        }
    }

    if (gma_last_block(filled_band, filled_block)) {
        fprintf(stderr, "%ld cells changed\n", rv->count_in_this_loop_of_band);
    }

    if (rv->count_in_this_loop_of_band)
        return 4;
    else
        return 2;
}

// band2 = flow directions
// band1 = upstream area = 1 + cells upstream
template<typename data1_t, typename data2_t>
int gma_upstream_area(gma_band<data1_t> band1, gma_block *block1, gma_band<data2_t> band2, gma_object_t **retval, gma_object_t*) {
    gma_retval_init(gma_band_iterator_t, rv, );
    if (gma_first_block(block1))
        rv->new_loop();
    int border_block = is_border_block(band1, block1);
    gma_cell_index i1;
    for (i1.y = 0; i1.y < block1->h; i1.y++) {
        for (i1.x = 0; i1.x < block1->w; i1.x++) {
            int border_cell = is_border_cell(block1, border_block, i1);

            // upstream area is already computed
            if (gma_block_cell(data1_t, block1, i1) > 0)
                continue;

            int upstream_neighbors = 0;
            int upstream_area = 0;

            gma_cell_index in = gma_cell_first_neighbor(i1);
            for (int neighbor = 1; neighbor < 9; neighbor++) {
                gma_cell_move_to_neighbor(in, neighbor);

                gma_cell_index i1n;
                gma_block *block1n = gma_index12index2(band1, block1, in, band1, &i1n);
                // neighbor is outside (or later also no data)
                if (!block1n)
                    continue;

                gma_cell_index i2;
                gma_block *block2 = gma_index12index2(band1, block1, in, band2, &i2);
                if (!block2)
                    continue;

                data2_t tmp2 = gma_block_cell(data2_t, block2, i2);
                // if this neighbor does not point to us, then we're not interested
                if (abs(tmp2 - neighbor) != 4)
                    continue;

                upstream_neighbors++;

                data1_t tmp1 = gma_block_cell(data1_t, block1n, i1n);
                // if the neighbor's upstream area is not computed, then we're done
                if (tmp1 == 0) {
                    upstream_neighbors = -1;
                    break;
                }

                upstream_area += tmp1;

            }

            if (upstream_neighbors == -1) {
                continue;
            } else if (upstream_neighbors == 0) {
                upstream_area = 1;
            } else if (upstream_neighbors > 0 && upstream_area == 0) {
                continue;
            }

            rv->add();
            gma_block_cell(data1_t, block1, i1) = upstream_area;

        }
    }

    if (gma_last_block(band1, block1))
        fprintf(stderr, "Upstream area of %ld cells computed.\n", rv->count_in_this_loop_of_band);

    if (rv->count_in_this_loop_of_band)
        return 4;
    else
        return 2;
}

template<typename catchment_t, typename fd_t>
int gma_catchment(gma_band<catchment_t> catchment_band, gma_block *catchment_block, gma_band<fd_t> band_fd, gma_object_t **retval, gma_object_t *arg) {
    gma_retval_init(gma_band_iterator_t, rv,  );
    if (gma_first_block(catchment_block))
        rv->new_loop();

    gma_cell_index i;
    gma_cell_p<catchment_t> *cell = (gma_cell_p<catchment_t> *)arg; // check?

    for (i.y = 0; i.y < catchment_block->h; i.y++) {
        for (i.x = 0; i.x < catchment_block->w; i.x++) {

            if (gma_block_cell(catchment_t, catchment_block, i) == cell->value()) continue;

            // if this is the outlet cell, mark
            // global cell index
            int x = catchment_block->index.x * catchment_band.w_block + i.x;
            int y = catchment_block->index.y * catchment_band.h_block + i.y;
            if (cell->x() == x && cell->y() == y) {
                gma_block_cell(catchment_t, catchment_block, i) = cell->value();
                rv->add();
                continue;
            }

            // if this flows into a marked cell, then mark this
            fd_t my_dir;
            gma_value_from_other_band<catchment_t,fd_t>(catchment_band, catchment_block, i, band_fd, &my_dir);

            gma_cell_index id = gma_cell_first_neighbor(i);
            for (int neighbor = 1; neighbor <= my_dir; neighbor++) {
                gma_cell_move_to_neighbor(id, neighbor);
            }

            catchment_t my_down;
            if (!gma_value_from_other_band<catchment_t>(catchment_band, catchment_block, id, catchment_band, &my_down))
                continue;

            if (my_down == cell->value()) {
                gma_block_cell(catchment_t, catchment_block, i) = cell->value();
                rv->add();
            }

        }
    }

    if (gma_last_block(catchment_band, catchment_block))
        fprintf(stderr, "%ld cells added\n", rv->count_in_this_loop_of_band);

    if (rv->count_in_this_loop_of_band)
        return 4;
    else
        return 2;
}


// focal distance & cache updates might be best done in callback since the knowledge is there
// unless we want to have focal distance in user space too
// anyway, focal area may be needed only in b2 or both in b2 and b1
template<typename type1, typename type2>
void gma_two_bands_proc(GDALRasterBand *b1, gma_two_bands_callback<type1,type2> cb, GDALRasterBand *b2, gma_object_t **retval, gma_object_t *arg, int focal_distance) {

    gma_band<type1> band1 = gma_band_initialize<type1>(b1);
    gma_band<type2> band2 = gma_band_initialize<type2>(b2);
    gma_block_index i;

    int iterate = 1;
    while (iterate) {
        iterate = 0;
        for (i.y = 0; i.y < band1.h_blocks; i.y++) {
            for (i.x = 0; i.x < band1.w_blocks; i.x++) {

                gma_band_add_to_cache(&band1, i);
                gma_block *block1 = gma_band_get_block(band1, i);
                CPLErr e = gma_band_update_cache(&band1, band1, block1, focal_distance);

                e = gma_band_update_cache<type1,type2>(&band2, band1, block1, focal_distance);

                int ret = cb.fct(band1, block1, band2, retval, arg);
                switch (ret) {
                case 0: return;
                case 1: break;
                case 2:
                    e = gma_band_write_block(band1, block1);
                    break;
                case 3:
                    e = gma_band_write_block(band1, block1);
                    iterate = 1;
                    break;
                case 4:
                    e = gma_band_write_block(band1, block1);
                    iterate = 2;
                    break;
                }
            }
        }
        if (iterate == 1) // band 2 <- band 1; new band 1
            gma_band_iteration<type1,type2>(&band1, &band2);
    }
    gma_band_empty_cache(&band1);
    gma_band_empty_cache(&band2);
}

gma_object_t *gma_two_bands(GDALRasterBand *b1, gma_two_bands_method_t method, GDALRasterBand *b2, gma_object_t *arg) {
    gma_object_t *retval = NULL;
    // b1 is changed, b2 is not
    if (b1->GetXSize() != b2->GetXSize() || b1->GetYSize() != b2->GetYSize()) {
        fprintf(stderr, "The sizes of the rasters should be the same.\n");
        return NULL;
    }
    switch (method) {
    case gma_method_assign_band: // b1 = b2
        if (arg) { // arg is 
            if (arg->get_class() != gma_logical_operation)
                goto wrong_argument_class;
            /*
            if (b2->GetRasterDataType() != (gma_logical_operation_p*)arg->get_datatype())
                goto wrong_argument_class;
            */
        }
        type_switch_bb(gma_assign_band, 0);
        break;
    case gma_method_add_band: // b1 += b2
        type_switch_bb(gma_add_band, 0);
        break;
    case gma_method_subtract_band:
        type_switch_bb(gma_subtract_band, 0);
        break;
    case gma_method_multiply_by_band: // b1 *= b2
        type_switch_bb(gma_multiply_by_band, 0);
        break;
    case gma_method_divide_by_band:
        type_switch_bb(gma_divide_by_band, 0);
        break;
    case gma_method_modulus_by_band:
        type_switch_bb(gma_modulus_by_band, 0);
        break;
    case gma_method_zonal_min: // b1 = zones, b2 = values
        type_switch_ib(gma_zonal_min, 0);
        break;
    case gma_method_zonal_max: // b1 = zones, b2 = values
        type_switch_ib(gma_zonal_max, 0);
        break;
    case gma_method_rim_by8: // rims <- areas
        type_switch_ii(gma_rim_by8, 1);
        break;
    case gma_method_D8: // fd <- dem
        // compute flow directions from DEM
        type_switch_ib(gma_D8, 1);
        break;
    case gma_method_route_flats: {  // fd, dem
        // iterative method to route flats in fdr
        // datatypes must be the same in iteration
        type_switch_ib(gma_route_flats, 1);
        break;
    }
    case gma_method_fill_depressions: { // b1 = filled, b2 = dem
        // compute max of b2
        gma_object_t *max = gma_compute_value(b2, gma_method_get_max);
        // set b1 to it
        gma_with_arg(b1, gma_method_assign, max);
        type_switch_bb(gma_fill_depressions, 1);
        break;
    }
    case gma_method_upstream_area: // ua, fd
        type_switch_bi(gma_upstream_area, 1);
        break;
    case gma_method_catchment: // mark into b1 the catchment with arg (), b2 contains fd
        type_switch_ii(gma_catchment, 1);
        break;
    default:
        goto unknown_method;
    }
    return retval;
not_implemented_for_these_datatypes:
    fprintf(stderr, "Not implemented for these datatypes <%i,%i>.\n", b1->GetRasterDataType(), b2->GetRasterDataType());
    return NULL;
unknown_method:
    fprintf(stderr, "Unknown method.\n");
    return NULL;
wrong_argument_class:
    fprintf(stderr, "Wrong class in argument.\n");
    return NULL;
}
