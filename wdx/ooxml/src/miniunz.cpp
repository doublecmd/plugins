#include "miniunz.h"

using namespace std;

string UnzipItem(unzFile hz, const char *file_name)
{
    char *ibuf;
    string tm = "";
    unz_file_info ze;

    if (unzLocateFile(hz, file_name, 0) == UNZ_OK)
    {
        if (unzGetCurrentFileInfo(hz, &ze, NULL, 0, NULL, 0, NULL, 0) == UNZ_OK)
        {
            ibuf = new char[ze.uncompressed_size];

            if (unzOpenCurrentFile(hz) == UNZ_OK)
            {
                if (unzReadCurrentFile(hz, ibuf, ze.uncompressed_size) > 0)
                {
                    tm = ibuf;
                }
                unzCloseCurrentFile(hz);
            }

            delete[] ibuf;
        }
    }
    return tm;
}

string UnzipItem(const char *zip_file, const char *file_name)
{
    unzFile hz;
    string tm = "";

    if ( (hz = unzOpen(zip_file) ) != NULL )
    {
        tm = UnzipItem(hz, file_name);

        unzClose(hz);
    }
    return tm;
}
