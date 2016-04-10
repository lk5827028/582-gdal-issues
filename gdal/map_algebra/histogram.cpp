#include "gdal_map_algebra.h"

int usage() {
    fprintf(stderr, "Usage 1): histogram [raster dataset] [mode]\n");
    fprintf(stderr, "      2): histogram [raster dataset] [mode] [number of bins]\n");
    fprintf(stderr, "      3): histogram [raster dataset] [mode] [number of bins] [min] [max]\n");
    fprintf(stderr, "      4): histogram [raster dataset] [mode] [max of bin 1] [max of bin 2] ...\n");
    fprintf(stderr, "Mode: 1: cell value => count\n");
    fprintf(stderr, "      2: bin => count\n");
    fprintf(stderr, "      3: bin => count\n");
    fprintf(stderr, "      4: bin => count\n");

    fprintf(stderr, "Bins are from a to b: (a,b]. a of the first bin is always -inf and b of the\n");
    fprintf(stderr, "last bin is always inf. Therefore, in usage 3 the min and max are not a or b\n");
    fprintf(stderr, "of any bin.\n");
    return 1;
}

int main(int argc, char *argv[]) {
    GDALAllRegister();
    if (argc < 3) return usage();
    GDALDataset *ds = (GDALDataset*)GDALOpen(argv[1], GA_ReadOnly);
    GDALRasterBand *b = ds->GetRasterBand(1);

    int mode = atoi(argv[2]);
    
    gma_histogram_t *hm;
    switch (mode) {
    case 1: {
        // histogram of all values,  works only for integer bands
        hm = (gma_histogram_t*)gma_compute_value(b, gma_method_histogram, NULL);
        break;
    }
    case 2: {
        if (argc < 4) return usage();
        int n = atoi(argv[3]);
        // histogram in n bins between min and max
        gma_pair_t *arg = (gma_pair_t *)gma_new_object(b, gma_pair);
        gma_number_t *tmp = (gma_number_t *)gma_new_object(b, gma_integer);
        tmp->set_value(n);
        arg->set_first(tmp);
        arg->set_second(gma_compute_value(b, gma_method_get_range));
        hm = (gma_histogram_t*)gma_compute_value(b, gma_method_histogram, arg);
        break;
    }
    case 3: {
        if (argc < 6) return usage();
        int n = atoi(argv[3]);
        double min = atof(argv[4]);
        double max = atof(argv[4]);
        gma_pair_t *arg = (gma_pair_t *)gma_new_object(b, gma_pair);
        gma_number_t *tmp = (gma_number_t *)gma_new_object(b, gma_integer);
        tmp->set_value(n);
        arg->set_first(tmp);
        gma_pair_t *tmp2 = (gma_pair_t *)gma_new_object(b, gma_range);
        ((gma_number_t*)(tmp2->first()))->set_value(min);
        ((gma_number_t*)(tmp2->second()))->set_value(max);
        arg->set_second(tmp2);
        hm = (gma_histogram_t*)gma_compute_value(b, gma_method_histogram, arg);
        break;
    }
    case 4: {
        if (argc < 4) return usage();
        gma_bins_t *arg = (gma_bins_t *)gma_new_object(b, gma_bins);
        int i = 3;
        while (i < argc) {
            arg->push(atof(argv[i]));
            i++;
        }
        hm = (gma_histogram_t*)gma_compute_value(b, gma_method_histogram, arg);
        break;
    }
    default:
        return usage();
    }
    print_histogram(hm);
    delete hm;
}
