#include "gdal_map_algebra_private.h"

typedef int (*gma_compute_value_callback)(gma_band, gma_block*, gma_object_t**, gma_object_t*);

template<typename datatype>
int gma_get_min(gma_band band, gma_block *block, gma_object_t **retval, gma_object_t *arg) {
    gma_retval_init(gma_number_p<datatype>, rv, );
    gma_cell_index i;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype x = gma_block_cell(datatype, block, i);
            if (!rv->defined() || x < rv->value())
                rv->set_value(x);
        }
    }
    return 1;
}

template<typename datatype>
int gma_get_max(gma_band band, gma_block *block, gma_object_t **retval, gma_object_t *arg) {
    gma_retval_init(gma_number_p<datatype>, rv, );
    gma_cell_index i;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype x = gma_block_cell(datatype, block, i);
            if (!rv->defined() || x > rv->value())
                rv->set_value(x);
        }
    }
    return 1;
}

template<typename datatype>
int gma_get_range(gma_band band, gma_block *block, gma_object_t **retval, gma_object_t *arg) {
    gma_pair_p<gma_number_p<datatype>*,gma_number_p<datatype>* > *rv;
    if (*retval == NULL) {
        rv = new gma_pair_p<gma_number_p<datatype>*,gma_number_p<datatype>* >(new gma_number_p<datatype>, new gma_number_p<datatype>);
        *retval = rv;
    } else
        rv = (gma_pair_p<gma_number_p<datatype>*,gma_number_p<datatype>* >*)*retval;
    gma_number_p<datatype>* min = (gma_number_p<datatype>*)rv->first();
    gma_number_p<datatype>* max = (gma_number_p<datatype>*)rv->second();
    gma_cell_index i;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype x = gma_block_cell(datatype, block, i);
            if (!min->defined() || x < min->value())
                min->set_value(x);
            if (!max->defined() || x > max->value())
                max->set_value(x);
        }
    }
    return 1;
}

template<typename datatype>
int gma_compute_histogram(gma_band band, gma_block *block, gma_object_t **retval, gma_object_t *arg) {
    gma_retval_init(gma_histogram_p<datatype>, hm, arg);
    gma_cell_index i;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype value = gma_block_cell(datatype, block, i);
            hm->increase_count_at(value);
        }
    }
    return 1;
}

template<typename datatype>
int gma_zonal_neighbors(gma_band band, gma_block *block, gma_object_t **retval, gma_object_t *) {
    gma_retval_init(gma_hash_p<datatype COMMA gma_hash_p<datatype COMMA gma_number_p<int> > >, zn, );
    gma_cell_index i;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype me = gma_block_cell(datatype, block, i);
            gma_hash_p<datatype,gma_number_p<int> > *ns;
            if (zn->exists(me))
                ns = zn->get(me);
            else {
                ns = new gma_hash_p<datatype,gma_number_p<int> >;
                zn->put(me, ns);
            }
            gma_cell_index in = gma_cell_first_neighbor(i);
            for (int neighbor = 1; neighbor < 9; neighbor++) {
                gma_cell_move_to_neighbor(in, neighbor);
                datatype n;

                if (!gma_value_from_other_band<datatype>(band, block, in, band, &n)) {
                    // we are at border and this is outside
                    if (!ns->exists(-1))
                        ns->put((int32_t)-1, new gma_number_p<int>(1)); // using -1 to denote outside
                    continue;
                }

                if (n != me && !ns->exists(n))
                    ns->put(n, new gma_number_p<int>(1) );

            }

        }
    }
    return 1;
}

template<typename datatype>
int gma_get_cells(gma_band band, gma_block *block, gma_object_t **retval, gma_object_t *arg) {
    gma_retval_init(std::vector<gma_cell_t*>, cells, );
    gma_cell_index i;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype me = gma_block_cell(datatype, block, i);
            // global cell index
            int x = block->index.x * band.w_block + i.x;
            int y = block->index.y * band.h_block + i.y;
            if (me)
                cells->push_back(new gma_cell_p<datatype>(x, y, me));
        }
    }
    return 1;
}

void gma_proc_compute_value(GDALRasterBand *b, gma_compute_value_callback cb, gma_object_t **retval, gma_object_t *arg, int focal_distance) {
    gma_band band = gma_band_initialize(b);
    gma_block_index i;
    for (i.y = 0; i.y < band.h_blocks; i.y++) {
        for (i.x = 0; i.x < band.w_blocks; i.x++) {
            gma_band_add_to_cache(&band, i);
            gma_block *block = gma_band_get_block(band, i);
            CPLErr e = gma_band_update_cache(&band, band, block, focal_distance);
            int ret = cb(band, block, retval, arg);
            switch (ret) {
            case 0: return;
            case 1: break;
            case 2: {
                CPLErr e = gma_band_write_block(band, block);
            }
            }
        }
    }
}

gma_object_t *gma_compute_value(GDALRasterBand *b, gma_method_compute_value_t method, gma_object_t *arg) {
    gma_object_t *retval = NULL;
    switch (method) {
    case gma_method_get_min:
        type_switch_single(gma_get_min, 0);
        break;
    case gma_method_get_max:
        type_switch_single(gma_get_max, 0);
        break;
    case gma_method_get_range:
        type_switch_single(gma_get_range, 0);
        break;
    case gma_method_histogram:
        if (arg == NULL) {
            type_switch_single_i(gma_compute_histogram, 0);
        } else {
            type_switch_single(gma_compute_histogram, 0);
        }
        break;
    case gma_method_zonal_neighbors: {
        type_switch_single(gma_zonal_neighbors, 1);

        // convert retval from gma_hash_p<gma_hash_p<gma_number_p<int> > > *
        // to vector of pairs of number and vector of numbers ??

        break;
    }
    case gma_method_get_cells: {
        type_switch_single(gma_get_cells, 0);
        break;
    }
    default:
        goto unknown_method;
    }
    return retval;
not_implemented_for_this_datatype:
    fprintf(stderr, "Not implemented for this datatype.\n");
    return NULL;
unknown_method:
    fprintf(stderr, "Unknown method.\n");
    return NULL;
}
