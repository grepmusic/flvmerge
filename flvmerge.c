#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include <malloc.h> // uncomment this line if you are using windows

typedef unsigned char byte;

typedef struct {
  byte type[3];
  byte version;
  byte typeFlag;
  byte headerLength[4];
} FlvHeader;

typedef struct {
  byte tagType;
  byte dataSize[3];
  byte timestamp[3];
  byte timestamp_extension;
  byte streamID[3];
  // byte tagData[ dataSize ]
} FlvTag;

// determine whether current system is big endian(network, media, etc) or not, e.g. int 1 is stored as "00 00 00 01" in
// big endian machine, while "01 00 00 00" in little endian machine, if we assume that sizeof(int) = 4 and the left
// address is smaller than the next(right) one
int is_big_endian() {
  int i = 0x1;
  return *(char*)&i != '\x1';
}

// exit program with exit code exitStatus before printing msg to stderr
void quit(char* msg, int exitStatus) {
  fprintf(stderr, "%s", msg);
  exit(exitStatus);
}

// convert a raw integer which is read from FLV file to which endian current system fits
int intval(byte* bits, int size) {
  int i, ret = 0;
  if (bits == NULL || size < 1 || size > 4) {
    quit("invalid bits(is NULL?) or size(out of [1,4]?) when calling intval\n", 1);
  }
  if (is_big_endian()) {
      return *(int*)bits;
  }
  for (i = 0; i < size; i++)
    ret = (int)bits[i] + (ret << 8);
  return ret;
}

// convert an integer stored as which endian current system fits to raw in FLV file
byte* byteval(int value, int size) {
  static byte bits[4] = {0};
  byte* p = (byte*)&value;
  int i;
  if (size < 1 || size > 4) {
    quit("invalid size(out of [1,4]?) when calling byteval\n", 1);
  }
  if (is_big_endian()) {
      *(int*)bits= value;
  } else {
      for (i=0; i < 4; ++i)
        bits[i] = p[3-i];
  }
  return bits + 4 - size;
}

// same as intval, just for double here
double doubleval(byte* bits) {
  static byte reverse_bits[8] = {0};
  int i;
  if (bits == NULL)
    quit("invalid bits(is NULL?)\n", 1);
  if (is_big_endian()) {
    *(double*)reverse_bits = *(double*)bits;
  } else {
    for(i = 0; i < 8; ++i)
      reverse_bits[i] = bits[7 - i];
  }
  return *(double*)reverse_bits;
}

// same as byteval, just for double here
byte* bytevaldouble(double value) {
  static byte bits[8] = {0};
  byte* p = (byte*)&value;
  int i;
  if (is_big_endian()) {
    *(double*)bits = value;
  } else {
    for (i = 0; i < 8; ++i)
      bits[i] = p[7-i];
  }
  return bits;
}

// return header if successfully, otherwise return NULL
FlvHeader* flv_header_read(FILE* fp, FlvHeader* header) {
  return fread(header, sizeof(FlvHeader), 1, fp) == 1 ? header : NULL;
}

// check if flv header is valid so that we can determine whether we need do merge
int flv_is_valid_header(FlvHeader* header) {
  return header && header->type[0] == 'F' && header->type[1] == 'L' && header->type[2] == 'V'
    && ((header->typeFlag | 5) == 5);
}

// read an flv tag from file fp points and save tag [meta] to tag, tag data size to dataSize, previous tag size to
// previousSize, return pure tag data
// CAUTION: this function will reserve the last allocated memory by returning a pointer, so memery leak is produced
// but only ONE leak, you can free it simply by 'byte* data=flv_tag_read(fp,...); /* some operation */ free(data);'
// but REMEMBER NOT to call flv_tag_read again after the free operation unless you wanna get 'segment fault'-like error, etc.
byte* flv_tag_read(FILE* fp, FlvTag* tag, int* dataSize, int* previousSize) {
  static byte* _tagData = NULL;
  static int _dataSize = 0; // store the length of _tagData
  int tagSize = 0, countread = fread(tag, sizeof(FlvTag), 1, fp);
  if (countread != 1)
    return NULL;
  tagSize = intval(tag->dataSize, 3);

  if (_tagData == NULL || _dataSize < tagSize) { // if _tagData is not allocated OR if _tagData is not enough, try to allocate for _tagData again
    if(_tagData) // but should free the old _tagData before allocates memery to it
      free(_tagData);
    _tagData = (byte*)malloc(tagSize * sizeof(byte));
  }
  
  if (fread(_tagData, sizeof(byte), tagSize, fp) != tagSize || 
    fread(previousSize, sizeof(int), 1, fp) != 1 ) {
    quit("FLV tag data(broken tag data or broken previous size?) is broken.\n", 1);
  }
  *dataSize = _dataSize = tagSize;
  *previousSize = *(int*)byteval(*previousSize, 4);
  return _tagData;
}

// use the most stupid searching algorithm to search binary data search in binary data data
// return index in data if found search, otherwise return -1
int stupid_byte_indexof(byte* search, int searchLength, byte* data, int dataSize) {
  int i, j, end = dataSize - searchLength, found;
  if (search == NULL || data == NULL || end < 0 || searchLength < 1)
    quit("invalid arguments when searching", 1);
  for (i=0; i < end; ++i) {
    found = 1;
    for(j=0; j < searchLength; ++j)
      if (data[j] != search[j]) {
        found = 0;
        break;
      }
    if (found)
      return i;
    data++;
  }
  return -1;
}

// strip keyframes data in script data tag and rewrite the hasKeyframes to false
byte* flv_scriptdata_strip_keyframes(FlvTag* tag, byte* scripttagData, int* dataSize) {
  byte hasKeyframes[] = {'h', 'a', 's', 'K', 'e', 'y', 'f', 'r', 'a', 'm', 'e', 's', '\x1'};
  byte keyframes[] = {'\x0', '\x9', 'k', 'e', 'y', 'f', 'r', 'a', 'm', 'e', 's', '\x3'};
  byte* ds = NULL;
  int len = sizeof(hasKeyframes)/sizeof(byte);
  int index;

  if (! tag || tag->tagType != 0x12 || ! scripttagData || ! dataSize) {
    quit("can't strip non-scriptdata's[null or video/audio tag data?] keyframes or null pointer", 1);
  }
  
  index = stupid_byte_indexof(hasKeyframes, len, scripttagData, *dataSize - 1);
  if (index != -1)
    scripttagData[index + len] = '\x0';
  index = stupid_byte_indexof(keyframes, sizeof(keyframes)/sizeof(byte), scripttagData, *dataSize);
  if (index != -1) {
    *dataSize = index;
    ds = byteval(index, 3);
    tag->dataSize[0] = ds[0];
    tag->dataSize[1] = ds[1];
    tag->dataSize[2] = ds[2];
  }
  return scripttagData;
}

// write an flv tag to file fp points with tag [meta] tag, tag data tagData and previous tag size previousSize
// return bytes written if successful, otherwise return 0
int flv_tag_write(FILE* fp, FlvTag* tag, byte* tagData, int* dataSize, int* previousSize) {
  if (
    fwrite(tag, sizeof(FlvTag), 1, fp) != 1 ||
    fwrite(tagData, sizeof(byte), *dataSize, fp) != *dataSize ||
    fwrite(previousSize, sizeof(int), 1, fp) != 1
    ) {
    return 0;
  }
  return sizeof(FlvTag) + *dataSize * sizeof(byte) + sizeof(int);
}

// get duration from an flv SCRIPT tag data(pure data) and save duration index where we found 
// duration in FLV file to offset if offset is not NULL
double flv_tag_get_duration(byte* tagData, int dataSize, int* offset) {
  // make sure tag is script tag, that is: tag.tagType == 0x12
  byte search[9] = { 'd', 'u', 'r', 'a', 't', 'i', 'o', 'n', '\0' };
  int index = stupid_byte_indexof(search, 9, tagData, dataSize);

  if (index == -1) {
    quit("Sorry, can't get flv meta duration.", 1);
  }

  index += sizeof(search)/sizeof(byte);
  if (offset)
    *offset = index;
  return doubleval(tagData + index);
}

// get timestamp from an flv tag [meta]
int flv_tag_get_timestamp(FlvTag* tag) {
  if (! tag)
    return -1;
  return ((int)(tag->timestamp_extension) << 24) + intval(tag->timestamp, 3);
}

// set timestamp to an flv tag [meta]
int flv_tag_set_timestamp(FlvTag* tag, int timestamp) {
  if (! tag || timestamp < 0)
    return -1;
  tag->timestamp_extension = timestamp >> 24;
  memcpy(tag->timestamp, byteval(timestamp & 0x00FFFFFF, 3), 3);
  return timestamp;
}

int main(int argc, char* argv[]) {

  FlvHeader header;
  FlvTag tag;
  byte* tagData;
  FILE *fpdst = NULL, *fpsrc = NULL;
  int i = 0, srccount = argc - 2, headerLength, duration_index = 0, 
    prevSize, dataSize, offset, foundduration = 0, zero = 0, basetimestamp[2], lasttimestamp[2] = {0};
  char** src = argv + 2;
  double duration = 0.0;

  int bts = 0;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s flvtobesaved 1stflv [2ndflv [3rdflv [...]]]\n", argv[0]);
    exit(1);
  }
  if ((fpdst = fopen(argv[1], "wb")) == NULL) {
    fprintf(stderr, "Can't write to file '%s'\n", argv[1]);
    exit(1);
  }
  
  while (i < srccount) {
    if ((fpsrc = fopen(src[i], "rb")) == NULL) {
      fprintf(stderr, "Can't open file '%s'\n", src[i]);
      exit(1);
    }
    
    if(! flv_header_read(fpsrc, &header) || ! flv_is_valid_header(&header)) {
      fprintf(stderr, "The header of file '%s' is broken or is not FLV header.\n", src[i]);
      exit(1);
    }

    if (i == 0) {
      fwrite(&header, sizeof(FlvHeader), 1, fpdst);
      fwrite(&zero, sizeof(int), 1, fpdst); // the first previous tag size is 0
      duration_index = sizeof(FlvHeader);
    }

    headerLength = intval(header.headerLength, 4);
    
    if (0 != fseek(fpsrc, headerLength+4, SEEK_SET)) { // skip to real flv tag data(skip the first previous tag size, +4)
      fprintf(stderr, "The first previousSize(should be 0) of file '%s' is broken.\n", src[i]);
      exit(1);
    }

    bts = (int)(duration * 1000);
    basetimestamp[0] = lasttimestamp[0];
    basetimestamp[1] = lasttimestamp[1];
    if (bts < basetimestamp[0])
      bts = basetimestamp[0];
    if (bts < basetimestamp[1])
      bts = basetimestamp[1];
    foundduration = 0;

    while (tagData = flv_tag_read(fpsrc, &tag, &dataSize, &prevSize)) {

      if (tag.tagType == 0x12 && ! foundduration) { // if script data and duration not found, try to get duration
        duration += flv_tag_get_duration(tagData, dataSize, &offset);
        foundduration = 1;
        if (i == 0) { // prepare the script data for writing, we choose the first FLV file header as sample
          duration_index += 4 + sizeof(FlvTag) + offset;
          
          flv_scriptdata_strip_keyframes(&tag, tagData, &dataSize);
          
          flv_tag_write(fpdst, &tag, tagData, &dataSize, &prevSize);
        }
      } else if (tag.tagType == 0x8 || tag.tagType == 0x9) {

        lasttimestamp[tag.tagType - 0x8] = bts + flv_tag_get_timestamp(&tag);
        flv_tag_set_timestamp(&tag, lasttimestamp[tag.tagType - 0x8]);

        flv_tag_write(fpdst, &tag, tagData, &dataSize, &prevSize);
        if (i == 0 && ! foundduration) {
          duration_index += 4 + sizeof(FlvTag) + dataSize;
        }
      }
    }

    //fprintf(stdout, "base: %d, last: %d\n", basetimestamp[0], lasttimestamp[0]);
    printf("completely merging file '%s' to '%s'\n", src[i], argv[1]);

    fclose(fpsrc);

    ++i;
  }
  if (0 != fseek(fpdst, duration_index, SEEK_SET))
    quit("can't seek to duration\n", 1);
  fwrite(bytevaldouble(duration), 1, 8, fpdst); // save real duration to file
  fclose(fpdst);

  return 0;
}
