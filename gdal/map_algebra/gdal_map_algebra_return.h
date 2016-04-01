typedef int (*gma_compute_value_callback)(gma_band band, gma_block *block, void*);

template<typename datatype>
int gma_get_min(gma_band band, gma_block *block, void *arg) {
    datatype mn = *(datatype*)arg;
    gma_cell_index i;
    int f = 1;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype x = gma_block_cell(datatype, block, i);
            if (f || x < mn) {
                f = 0;
                mn = x;
            }
        }
    }
    *(datatype*)arg = mn;
    return 1;
}

template<typename datatype>
int gma_get_max(gma_band band, gma_block *block, void *arg) {
    datatype mx = *(datatype*)arg;
    gma_cell_index i;
    int f = 1;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype x = gma_block_cell(datatype, block, i);
            if (f || x > mx) {
                f = 0;
                mx = x;
            }
        }
    }
    *(datatype*)arg = mx;
    return 1;
}

// histogram should be gma_hash<gma_int>

template<typename datatype>
int gma_histogram(gma_band band, gma_block *block, void *histogram) {
    gma_hash<gma_int> *hm = (gma_hash<gma_int>*)histogram;
    gma_cell_index i;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype key = gma_block_cell(datatype, block, i);
            if (hm->exists(key))
                hm->get(key)->add(1);
            else
                hm->put(key, new gma_int(1));
        }
    }
    return 1;
}

template<typename datatype>
int gma_zonal_neighbors(gma_band band, gma_block *block, void *zonal_neighbors) {
    gma_hash<gma_hash<gma_int> > *h = (gma_hash<gma_hash<gma_int> >*)zonal_neighbors;
    gma_cell_index i;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype me = gma_block_cell(datatype, block, i);
            gma_hash<gma_int> *ns;
            if (h->exists(me))
                ns = h->get(me);
            else {
                ns = new gma_hash<gma_int>;
                h->put(me, ns);
            }

            gma_cell_index in = gma_cell_first_neighbor(i);
            for (int neighbor = 1; neighbor < 9; neighbor++) {
                gma_cell_move_to_neighbor(in, neighbor);
                datatype n;

                if (!gma_value_from_other_band<datatype>(band, block, in, band, &n)) {
                    ns->put(-1, new gma_int(1));
                    continue;  // we are at border and this is outside
                }
                
                if (n != me && !ns->exists(n))
                    ns->put(n, new gma_int(1));
                
            }

        }
    }
    return 1;
}

template<typename datatype>
int gma_get_cells(gma_band band, gma_block *block, void *cells) {
    gma_array<gma_cell<datatype> > *c = (gma_array<gma_cell<datatype> > *)cells;
    gma_cell_index i;
    for (i.y = 0; i.y < block->h; i.y++) {
        for (i.x = 0; i.x < block->w; i.x++) {
            datatype me = gma_block_cell(datatype, block, i);
            // global cell index
            int x = block->index.x * band.w_block + i.x;
            int y = block->index.y * band.h_block + i.y;
            if (me) c->push(new gma_cell<datatype>(x, y, me));
        }
    }
    return 1;
}

template<typename datatype>
void gma_proc_compute_value(GDALRasterBand *b, gma_compute_value_callback cb, void *ret_val, int focal_distance) {
    gma_band band = gma_band_initialize(b);
    gma_block_index i;
    for (i.y = 0; i.y < band.h_blocks; i.y++) {
        for (i.x = 0; i.x < band.w_blocks; i.x++) {
            gma_band_add_to_cache(&band, i);
            gma_block *block = gma_band_get_block(band, i);
            CPLErr e = gma_band_update_cache(&band, band, block, focal_distance);
            int ret = cb(band, block, ret_val);
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

template <typename retval_t>
retval_t *gma_compute_value_object(GDALRasterBand *b, gma_method_compute_value_t method) {
    retval_t *retval = new retval_t;
    switch (method) {
    case gma_method_histogram:
        type_switch_single(gma_histogram, 0);
        break;
    case gma_method_zonal_neighbors:
        type_switch_single(gma_zonal_neighbors, 1);
        break;
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

template <typename retval_t>
retval_t gma_compute_value(GDALRasterBand *b, gma_method_compute_value_t method) {
    retval_t retval;
    switch (method) {
    case gma_method_get_min:
        type_switch_single2(gma_get_min, 0);
        break;
    case gma_method_get_max:
        type_switch_single2(gma_get_max, 0);
        break;
    default:
        goto unknown_method;
    }
    return retval;
not_implemented_for_this_datatype:
    fprintf(stderr, "Not implemented for this datatype.\n");
    return 0;
unknown_method:
    fprintf(stderr, "Unknown method.\n");
    return 0;
}
