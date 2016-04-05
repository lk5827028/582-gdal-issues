#include "gdal_map_algebra.h"

int main() {

    // test hash of a hash
    gma_hash_p<gma_hash_p<gma_number_p<int> > > *I;

    I = new gma_hash_p<gma_hash_p<gma_number_p<int> > >;

    gma_hash_p<gma_number_p<int> > *J;

    J = new gma_hash_p<gma_number_p<int> >;
    J->put(5, new gma_number_p<int>(1));
    J->put(6, new gma_number_p<int>(1));

    I->put(6, J);

    int n = I->size();
    int *keys = I->keys_sorted(n);
    for (int i = 0; i < n; i++) {
        printf("%i =>\n", keys[i]);

        J = (gma_hash_p<gma_number_p<int> > *)I->get(keys[i]);
        int n_j = J->size();
        int *keys_j = J->keys_sorted(n_j);
        for (int j = 0; j < n_j; j++) {
            printf("    %i\n", keys_j[j]);
        }
        CPLFree(keys_j);
    }
    CPLFree(keys);
    delete I;

    return 0;

    GDALAllRegister();
    srand(time(NULL));
    GDALDriver *d = GetGDALDriverManager()->GetDriverByName("MEM");
    int w_band = 16, h_band = 10;
    GDALDataset *ds = d->Create("", w_band, h_band, 2, GDT_Int32, NULL);
    GDALRasterBand *b = ds->GetRasterBand(1);
    gma_simple(b, gma_method_rand);
    gma_simple(b, gma_method_print);
    printf("\n");

    gma_number_t *x = (gma_number_t*)gma_new_object(b, gma_number);
    x->set_value(5);
    gma_with_arg(b, gma_method_add, x);
    gma_simple(b, gma_method_print);
    printf("\n");

    GDALRasterBand *b2 = ds->GetRasterBand(2);
    gma_simple(b2, gma_method_rand);
    gma_simple(b2, gma_method_print);
    printf("\n");

    gma_two_bands(b, gma_method_add_band, b2);
    gma_simple(b, gma_method_print);
    printf("\n");

    ds = d->Create("", w_band, h_band, 1, GDT_Float64, NULL);
    b = ds->GetRasterBand(1);
    gma_simple(b, gma_method_rand);
    gma_simple(b, gma_method_print);
    printf("\n");

    x = (gma_number_t*)gma_new_object(b, gma_number);
    x->set_value(1.1);
    gma_with_arg(b, gma_method_add, x);
    gma_simple(b, gma_method_print);
}
