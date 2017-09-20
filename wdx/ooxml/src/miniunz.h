#ifndef _miniunz_H
#define _miniunz_H

#include <string>
#include "unzip.h"

extern std::string UnzipItem(unzFile hz, const char *file_name);
extern std::string UnzipItem(const char *zip_file, const char *file_name);

#endif
