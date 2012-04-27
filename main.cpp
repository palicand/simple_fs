#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <ctime>
using namespace std;

#define FILENAME_LEN_MAX    28
#define DIR_ENTRIES_MAX     128
#define OPEN_FILES_MAX      8
#define SECTOR_SIZE         512
#define DEVICE_SIZE_MAX     ( 1024 * 1024 * 1024 )
#define DEVICE_SIZE_MIN     ( 8 * 1024 * 1024 )

/*#define __READ_DEBUG__
#define __WRITE_DEBUG__
*/
struct TFile
 {
   char            m_FileName[FILENAME_LEN_MAX + 1];
   int             m_FileSize;
 };

struct TBlkDev
 {
   int             m_Sectors;
   int          (* m_Read)( int, void *, int );
   int          (* m_Write)( int, const void *, int );
 };

#endif /* __PROGTEST__ */


int     FsCreate            ( struct TBlkDev * dev );
int     FsMount             ( struct TBlkDev * dev );
int     FsUmount            ( void );

int     FileOpen            ( const char     * fileName,
                              int              writeMode );
int     FileRead            ( int              fd,
                              void           * buffer,
                              int              len );
int     FileWrite           ( int              fd,   
                              const void     * buffer,  
                              int              len );
int     FileClose           ( int              fd ); 

int     FileDelete          ( const char     * fileName );

int     FileFindFirst       ( struct TFile   * info );
int     FileFindNext        ( struct TFile   * info );
int     FileSize            ( const char     * fileName );

const static int SECTORS_PER_BLOCK = 8;
const static int BLOCK_SIZE = SECTOR_SIZE * SECTORS_PER_BLOCK;
const static int EOC = -1;
const static int SYS = -2;
struct TFat
{
   /* data */
   int block_num;
   int* blocks;
   //na ktorom bloku zacina adresar
   int dir_block;
   //na ktorom bloku zacinaju data
   int data_block;
};

struct TEntry
{
   char filename[FILENAME_LEN_MAX+ 1];
   int start_block;
   int size;
};

struct TDir
{
   int files_total;
   TEntry files[DIR_ENTRIES_MAX];
};

struct TBuffer
{
   int buf_pos;
   char buffer[BLOCK_SIZE];
};

struct TOpenFile
{
   TBuffer* buffer;
   int current_block;
   int file_index;
   int mode;
   int file_pos;
};
//v tejto strkture mame vsetky informacie, ktore nam treba pocas behu programu,jej premenne inicilizueme pri mounte a deletneme pri umunte
struct TFileSys
{
   TFat table;
   TDir root;
   TOpenFile open_files[OPEN_FILES_MAX];
   TBlkDev dev;
   int last_file;

};

TFileSys* g_info;


void init_table(TFat& table, const TBlkDev& dev);
void init_dir(TDir& dir);
void init_entry(TEntry& entry);
void init_open_files_entry(int fd);

bool write_fat(const TFat& fat);
bool write_dir(const TDir& dir);
bool read_fat(TFat& fat);
bool read_dir(TDir& dir);

int write_from_buffer(int fd);
int copy_to_buffer(TBuffer& buffer, const char * data, int len, int src_pos);
int copy_from_buffer(int fd, char* data, int len, int src_pos);
int find_file(const char* filename);
int create_file(const char* filename);
void remove_file(int index);
int get_free_index();
int get_free_block();
int clear_file(int index);
int add_to_open_files(int file_index, int mode);
void close_all_files();
int read_to_buffer(int fd);

int write_block(int block, const TBuffer& buffer)
{
#ifdef __WRITE_DEBUG__
  cout << "writing to block " << block << endl;
#endif
   return g_info->dev.m_Write(block * SECTORS_PER_BLOCK, buffer.buffer, SECTORS_PER_BLOCK);
}
int read_block(int block, TBuffer& buffer)
{
#ifdef __READ_DEBUG__
  cout << "reading from block " << block << endl;
#endif
  return g_info->dev.m_Read(block * SECTORS_PER_BLOCK, buffer.buffer, SECTORS_PER_BLOCK);
}
int FsCreate( TBlkDev* dev)
{
   g_info = new TFileSys;
   g_info->dev = *dev;
   //pridame nejake zakladne data
   init_table(g_info->table, *dev);
   init_dir(g_info->root);

   write_fat(g_info->table);
   write_dir(g_info->root);
   delete [] g_info->table.blocks;
   delete g_info;
   return 1;
}

int FsMount( TBlkDev* dev)
{
   g_info = new TFileSys;
   g_info->dev = *dev;
   read_fat(g_info->table);
   read_dir(g_info->root);
   for(int i = 0; i < OPEN_FILES_MAX; i++)
      init_open_files_entry(i);
   return 1;
}

int FileOpen(const char* filename, int mode)
{
   int file = find_file(filename);
   int fd = -1;
   //nenasiel sa subor
   if(file == -1)
   {
      if(mode == 0)
      {
         return -1;
      }
      file = create_file(filename);
   }
   else
   {
      if(mode == 1)
         clear_file(file);
   }
   fd = add_to_open_files(file, mode);
   return fd;
}

int FileRead(int fd, void* data, int len)
{
  int dest_pos = 0;
  //kopirujeme bud kym sme nenaplnili uzivatelsky buffer, alebo kym mame co citat
  while((dest_pos != len) && (g_info->open_files[fd].file_pos != g_info->root.files[g_info->open_files[fd].file_index].size))
  {
    //sme na zaciatku, nacitame si buffer
    if(g_info->open_files[fd].buffer->buf_pos == 0)
    {
      //nacitame si cely blok
      read_to_buffer(fd);
    }
    int copied = copy_from_buffer(fd, (char*)data, len, dest_pos);
    dest_pos += copied;
    g_info->open_files[fd].file_pos += copied;
    g_info->open_files[fd].buffer->buf_pos += copied;
    if(g_info->open_files[fd].buffer->buf_pos == BLOCK_SIZE)
    {
      g_info->open_files[fd].buffer->buf_pos = 0;
      if(g_info->open_files[fd].current_block > 0)
        g_info->open_files[fd].current_block = g_info->table.blocks[g_info->open_files[fd].current_block];
    }
  }
  return dest_pos;
}


int FileWrite(int fd, const void* data, int len)
{
#ifdef __WRITE_DEBUG__
  cout << "current block: " << g_info->open_files[fd].current_block << endl;
#endif
   int src_pos = 0;
   while(src_pos != len)
   {
      //skopirujem data z uzivatelskeho bufferu do svojho
    int bytes_copied = copy_to_buffer(*g_info->open_files[fd].buffer, (char*)data, len, src_pos);
    //cout << "copied " << bytes_copied << " bytes" << endl;
    if(bytes_copied == 0)
      return 0;
    src_pos += bytes_copied;
    g_info->open_files[fd].buffer->buf_pos += bytes_copied;
    g_info->root.files[g_info->open_files[fd].file_index].size += bytes_copied;
    if(g_info->open_files[fd].buffer->buf_pos == BLOCK_SIZE)
         if(write_from_buffer(fd) == 0)
         {
          //cout << "shit happens\n";
          return 0;
        }
   }
   return src_pos;
}

int FileDelete(const char* fileName)
{
  int index = find_file(fileName);
  clear_file(index);
  g_info->table.blocks[g_info->root.files[index].start_block] = 0;
  remove_file(index);
   return 1;
}

int FileClose(int fd)
{
   if(g_info->open_files[fd].mode == 1)
      write_block(g_info->open_files[fd].current_block, *g_info->open_files[fd].buffer);
    else
      g_info->open_files[fd].file_pos = -1;
    g_info->open_files[fd].current_block = -1;
    g_info->open_files[fd].mode = -1;
    g_info->open_files[fd].file_index = -1;
    delete g_info->open_files[fd].buffer;
   return 1;
}

int FileSize(const char* filename)
{
  int index = find_file(filename);
  if(index == -1)
    return -1;
  return g_info->root.files[index].size;
}

int FileFindFirst(TFile* info)
{
  g_info->last_file = -1;
  return FileFindNext(info);
}

int FileFindNext(TFile* info)
{
  g_info->last_file++;
  while(g_info->last_file < DIR_ENTRIES_MAX && g_info->root.files[g_info->last_file].size == -1)
    g_info->last_file++;
  if(g_info->last_file >= DIR_ENTRIES_MAX)
    return 0;
  info->m_FileSize = g_info->root.files[g_info->last_file].size;
  strcpy(info->m_FileName, g_info->root.files[g_info->last_file].filename);
  return 1;
}

int FsUmount()
{
  close_all_files();
  write_fat(g_info->table);
  write_dir(g_info->root);
  delete [] g_info->table.blocks;
  delete g_info;
  return 1;
}

void close_all_files()
{
  for(int i = 0; i < OPEN_FILES_MAX; i++)
  {
    if(g_info->open_files[i].file_index != -1)
      FileClose(i);
  }
}

void init_table(TFat& table, const TBlkDev& dev)
{
   table.block_num = dev.m_Sectors / SECTORS_PER_BLOCK;
   table.blocks = new int[table.block_num];
   memset(table.blocks, 0, table.block_num * sizeof(int));
   table.dir_block = table.block_num*sizeof(int) / BLOCK_SIZE + 2;
   table.data_block = table.dir_block + (sizeof(TEntry) * DIR_ENTRIES_MAX) / BLOCK_SIZE + 1;
   for(int i = 0; i < table.data_block; i++)
   {
      table.blocks[i] = SYS;
   }
}

void init_dir(TDir& dir)
{
   for(int i = 0; i < DIR_ENTRIES_MAX; i++)
   {
      init_entry(dir.files[i]);
   }
   dir.files_total = 0;
}

void init_entry(TEntry& entry)
{
   memset(entry.filename, '\0', FILENAME_LEN_MAX + 1);
   entry.size = -1;
   entry.start_block = -1;
}

void init_open_files_entry(int fd)
{
   g_info->open_files[fd].buffer = NULL;
   g_info->open_files[fd].current_block = -1;
   g_info->open_files[fd].file_index = -1;
   g_info->open_files[fd].mode = -1;
   g_info->open_files[fd].file_pos = -1;
}

bool write_fat(const TFat& fat)
{
   char* buffer = new char[BLOCK_SIZE];
   memset(buffer, 0, BLOCK_SIZE);
   memcpy(buffer, &fat, sizeof(fat));
   g_info->dev.m_Write(0, buffer, SECTORS_PER_BLOCK);
   delete [] buffer;
   buffer = new char[(fat.dir_block - 1) * BLOCK_SIZE];
   memset(buffer, 0, (fat.dir_block - 1) * BLOCK_SIZE);
   memcpy(buffer, fat.blocks, fat.block_num*sizeof(int));
   g_info->dev.m_Write(SECTORS_PER_BLOCK, buffer, (fat.dir_block - 1) * SECTORS_PER_BLOCK);
   delete [] buffer;
   return true;
}

bool write_dir(const TDir& dir)
{
  char* buffer = new char[2*BLOCK_SIZE];
  memcpy(buffer, dir.files, DIR_ENTRIES_MAX * sizeof(TEntry));
  g_info->dev.m_Write(g_info->table.dir_block * SECTORS_PER_BLOCK, buffer, 2*SECTORS_PER_BLOCK);
  delete [] buffer;
  return true;
}

bool read_fat(TFat& fat)
{
  char* buffer = new char[BLOCK_SIZE];
  memset(buffer, 0, BLOCK_SIZE);
  g_info->dev.m_Read(0, buffer, SECTORS_PER_BLOCK);
  memcpy(&fat, buffer, sizeof(fat));
  delete [] buffer;
  fat.blocks = new int[fat.block_num];
  buffer = new char[(fat.dir_block - 1) * BLOCK_SIZE];
  memset(buffer, 0, (fat.dir_block - 1) * BLOCK_SIZE);
  g_info->dev.m_Read(SECTORS_PER_BLOCK, buffer, (fat.dir_block - 1) * SECTORS_PER_BLOCK);
  memcpy(fat.blocks, buffer, fat.block_num*sizeof(int));
  delete [] buffer;
  return true;
}

bool read_dir(TDir& dir)
{
  char* buffer = new char[2*BLOCK_SIZE];
  g_info->dev.m_Read(g_info->table.dir_block * SECTORS_PER_BLOCK, buffer, 2*SECTORS_PER_BLOCK);
  memcpy(dir.files, buffer, DIR_ENTRIES_MAX * sizeof(TEntry));
  delete [] buffer;
  return true;
}

int write_from_buffer(int fd)
{
   if(g_info->open_files[fd].current_block == -1)
      return 0;
   write_block(g_info->open_files[fd].current_block, *g_info->open_files[fd].buffer);
   int new_block = get_free_block();
   g_info->table.blocks[g_info->open_files[fd].current_block] = new_block;
   g_info->open_files[fd].current_block = new_block;
   if(new_block != -1)
   {
      g_info->table.blocks[g_info->open_files[fd].current_block] = EOC;
   }
   g_info->open_files[fd].buffer->buf_pos = 0;
   return 1;
}

int read_to_buffer(int fd)
{
  if(g_info->open_files[fd].current_block == -1 || g_info->open_files[fd].current_block == -2)
      return 0;
   #ifdef __READ_DEBUG__
    cout <<  fd << " ";
    #endif
   read_block(g_info->open_files[fd].current_block, *g_info->open_files[fd].buffer);
   #ifdef __READ_DEBUG__
   cout << "now on " << g_info->open_files[fd].current_block << " will go to " << g_info->table.blocks[g_info->open_files[fd].current_block] << endl;
   #endif
   
   return 1;
}

int copy_to_buffer(TBuffer& buffer, const char * data, int len, int src_pos)
{
  int remains = len - src_pos;
  //moze byt max 4096
  int fits_in_buffer = BLOCK_SIZE - buffer.buf_pos;
  int size_to_copy;
  if(fits_in_buffer > remains)
  {
    size_to_copy = remains;
  }
  else
  {
    size_to_copy = fits_in_buffer;
  }

  memcpy(buffer.buffer + buffer.buf_pos, data + src_pos, size_to_copy);

  return size_to_copy;
}

int find_file(const char* filename)
{
   int i = 0;
   while(i < DIR_ENTRIES_MAX && strcmp(g_info->root.files[i].filename, filename) != 0)
   {
      i++;
   }

   if(i == DIR_ENTRIES_MAX)
      return -1;
   return i;
}

int get_free_index()
{
   int i = 0;
   while(i < DIR_ENTRIES_MAX && g_info->root.files[i].size != -1)
   {
      i++;
   }
   if(i == DIR_ENTRIES_MAX)
      return -1;
   return i;
}

int get_free_block()
{
   int i = 0;
   while(i < g_info->table.block_num && g_info->table.blocks[i] != 0)
   {
      i++;
   }
   if(i == g_info->table.block_num)
      return -1;
   return i;
}

int get_free_fd()
{
   int i = 0;
   while(i < OPEN_FILES_MAX && g_info->open_files[i].file_index != -1)
   {
      i++;
   }
   if(i == OPEN_FILES_MAX)
      return -1;
   return i;
}

int create_file(const char* filename)
{
   int index = get_free_index();
   if(index == -1)
      return -1;
   g_info->root.files[index].size = 0;
   g_info->root.files[index].start_block = get_free_block();
   g_info->table.blocks[g_info->root.files[index].start_block] = EOC;
   strcpy(g_info->root.files[index].filename, filename);
   return index;
}

int add_to_open_files(int file_index, int mode)
{
   int fd = get_free_fd();
   if(fd == -1)
      return -1;
   g_info->open_files[fd].file_index = file_index;
   g_info->open_files[fd].current_block = g_info->root.files[file_index].start_block;
   g_info->open_files[fd].mode = mode;
   g_info->open_files[fd].buffer = new TBuffer;
   g_info->open_files[fd].buffer->buf_pos = 0;
   if(mode == 0)
   {
    g_info->open_files[fd].file_pos = 0;
   }
   memset(g_info->open_files[fd].buffer->buffer, 0, BLOCK_SIZE);
   return fd;
}

int clear_file(int index)
{
  if(index == -1)
    return -1;
  int current_block = g_info->root.files[index].start_block;
  int prev_block;
  //cout << "clearig file, " << current_block << endl;
  while(g_info->table.blocks[current_block] != EOC)
  {
    prev_block = current_block;
    current_block = g_info->table.blocks[current_block];
    g_info->table.blocks[prev_block] = 0;
  }
  g_info->table.blocks[current_block] = 0;
  g_info->table.blocks[g_info->root.files[index].start_block] = EOC;
  g_info->root.files[index].size = 0;
  return 1;
}

int copy_from_buffer(int fd, char* data, int len, int src_pos)
{
  int fits_in_buffer = BLOCK_SIZE - g_info->open_files[fd].buffer->buf_pos;
  int file_size = g_info->root.files[g_info->open_files[fd].file_index].size;
  int left_in_file = file_size - g_info->open_files[fd].file_pos;
  int remains = len - src_pos;
  int upper_limit = (left_in_file < remains) ? left_in_file : remains;
  int size_to_copy = (fits_in_buffer < upper_limit) ? fits_in_buffer : upper_limit;
  memcpy(data + src_pos, g_info->open_files[fd].buffer->buffer + g_info->open_files[fd].buffer->buf_pos, size_to_copy);
  return size_to_copy;
}

void remove_file(int index)
{
  memset(g_info->root.files[index].filename, '\0', FILENAME_LEN_MAX + 1);
  g_info->root.files[index].size = -1;
  g_info->root.files[index].start_block = -1;
}

#ifndef __PROGTEST__
/* Filesystem - sample usage.
 *
 * The testing of the fs driver requires a backend (simulating the underlying disk).
 * Next, tests of your fs implemetnation are needed. To help you with the implementation,
 * a sample backend is implemented in this file. It provides a quick-and-dirty
 * implementation of the underlying disk (simulated in a file) and a few Fs... function calls.
 *
 * The implementation in the real testing environment is different. The sample below is a
 * minimalistic disk backend which matches the required interface.
 *
 * You will have to add some FS testing. There is a few Fs... functions called from within
 * main(), however, the tests are incomplete. Once again, this is only a starting point.
 */

//#define DISK_SECTORS (8*1024*1024) / 512
 #define DISK_SECTORS 78985
static FILE  * g_Fp = NULL;

//-------------------------------------------------------------------------------------------------
/** Sample sector reading function. The function will be called by your fs driver implementation.
 * Notice, the function is not called directly. Instead, the function will be invoked indirectly
 * through function pointer in the TBlkDev structure.

 */
 void randContent(char* buffer, int sz)
{
  for(int i = 0; i < sz; i++)
  {
    char c = rand() % 25 + 97;
    buffer[i] = c;
  }
}
int                diskRead                                ( int               sectorNr,
                                                             void            * data,
                                                             int               sectorCnt )
 {
   if ( g_Fp == NULL ) return 0;
   if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS ) return 0;
   fseek ( g_Fp, sectorNr * SECTOR_SIZE, SEEK_SET );
   return fread ( data, SECTOR_SIZE, sectorCnt, g_Fp );
 }
//-------------------------------------------------------------------------------------------------
/** Sample sector writing function. Similar to diskRead
 */
int                diskWrite                               ( int               sectorNr,
                                                             const void      * data,
                                                             int               sectorCnt )
 {
   if ( g_Fp == NULL ) return 0;
   if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS ) return 0;
   fseek ( g_Fp, sectorNr * SECTOR_SIZE, SEEK_SET );
   return fwrite ( data, SECTOR_SIZE, sectorCnt, g_Fp );
 }
//-------------------------------------------------------------------------------------------------
/** A function which creates the file needed for the sector reading/writing functions above.
 * This function is only needed for the particular implementation above. It could be understand as
 * "buying a new disk".
 */
TBlkDev          * createDisk                              ( void )
 {
   char       buffer[SECTOR_SIZE];
   TBlkDev  * res = NULL;
   int        i;


   g_Fp = fopen ( "disk_content", "w+b" );
   if ( ! g_Fp ) return NULL;

   for ( i = 0; i < DISK_SECTORS; i ++ )
   {
    randContent(buffer, SECTOR_SIZE);
    if ( fwrite ( buffer, sizeof ( buffer ), 1, g_Fp ) != 1 )
     return NULL;
 }

   res              = new TBlkDev;
   memset(res, 0, sizeof(TBlkDev));
   res -> m_Sectors = DISK_SECTORS;
   res -> m_Read    = diskRead;
   res -> m_Write   = diskWrite;
   return res;
 }
//-------------------------------------------------------------------------------------------------
/** A function which opens the files needed for the sector reading/writing functions above.
 * This function is only needed for the particular implementation above. It could be understand as
 * "turning the computer on".
 */
TBlkDev          * openDisk                                ( void )
 {
   TBlkDev  * res = NULL;

   g_Fp = fopen ( "disk_content", "r+b" );
   if ( ! g_Fp ) return NULL;
   fseek ( g_Fp, 0, SEEK_END );
   if ( ftell ( g_Fp ) != DISK_SECTORS * SECTOR_SIZE )
    {
      fclose ( g_Fp );
      g_Fp = NULL;
      return NULL;
    }

   res              = new TBlkDev;
   res -> m_Sectors = DISK_SECTORS;
   res -> m_Read    = diskRead;
   res -> m_Write   = diskWrite;
   return res;
 }
//-------------------------------------------------------------------------------------------------
/** A function which releases resources allocated by openDisk/createDisk
 */
void               doneDisk                                ( TBlkDev         * dev )
 {
   delete dev;
   if ( g_Fp )
    {
      fclose ( g_Fp );
      g_Fp  = NULL;
    }
 }
//-------------------------------------------------------------------------------------------------


void randFN(char src[FILENAME_LEN_MAX + 1])
{
  randContent(src, FILENAME_LEN_MAX);
  src[FILENAME_LEN_MAX] = '\0';
}


 void printbuf(char* buf, int len)
 {
  for(int i = 0; i < len; i++)
  {
    printf("%c", buf[i]);
  }
  printf("\n");
 }

 void printAllF()
 {
    TFile  info;
    int i = 0;
    int ret;
    if ( (ret = FileFindFirst ( &info )) )
     do 
      { 
        printf ( "%d: %s %d\n", i, info . m_FileName, info . m_FileSize );
        i++;
      } while ((ret = FileFindNext ( &info )) );

 }

void getName( char name[29], const char * path ){
  int i, len = 0;
  
  i = (int) strlen( path ) - 1;
  
  while( path[i] != '/' && i > 0 ){
    i--; len++;
  }
  
  if( path[i] == '/' ) i++;
  else len++;
  
  if( len > 28 ) len = 28;
  
  strncpy( name, path + i, len );
  name[len] = 0;
}

int FileWriteR( int fd, const char * buffer, int len ){
  int wrote = 0;
  int chunk;
  const char * ptr = buffer;
  
  while( wrote < len ){
    chunk = rand() % (len % 4095 + 1);
    chunk = ( chunk < len - wrote ) ? chunk : len - wrote;
    
    printf( "Writing %d bytes of file %d bytes long.\n", chunk, len );
    FileWrite( fd, ptr, chunk );
    
    wrote += chunk;
    ptr += chunk;
  }
  
  return wrote;
}

int FileReadR( int fd, char * buffer, int len ){
  int read = 0;
  int chunk;
  char * ptr = buffer;
  
  while( read < len ){
    chunk = rand() % 4096;
    chunk = ( chunk < len - read ) ? chunk : len - read;
    
    printf( "Reading %d bytes of file %d bytes long.\n", chunk, len );
    FileRead( fd, ptr, chunk );
    
    read += chunk;
    ptr += chunk;
  }
  
  return read;
}
int main ( int argc, char** argv )
 {
   TBlkDev * dev;
   int fd[OPEN_FILES_MAX];
   int index;
   char files[OPEN_FILES_MAX][FILENAME_LEN_MAX + 1];
   srand(time(0));
   dev = createDisk();
   FsCreate(dev);
   FsMount(dev);
   int limit = rand() % 4569 + 2048;
   int ref_sz[OPEN_FILES_MAX] = {0};
   int written_sz[OPEN_FILES_MAX] = {0};
   int sz;
   int this_sz = 0;
   cout << "TEntry: " << sizeof(TEntry) << endl;
   for(int i = 0; i < OPEN_FILES_MAX; i++)
   {
     randFN(files[i]);
     cout << "openning file for write: " << files[i];
     fd[i] = FileOpen(files[i], 1);
     cout << "got fd " << fd[i] << endl;
   }
   char* bufer;
   for(int i = 0; i < 100; i++)
   {
     index = rand() % 8;
     sz = rand() % limit;
     bufer = new char[sz];
     randContent(bufer, sz);
     ref_sz[index] += sz;
     written_sz[index] += FileWrite(index, bufer, sz);
     delete [] bufer;
   }

   for(int i = 0; i < OPEN_FILES_MAX; i++)
   {
     FileClose(i);
     cout << ref_sz[i] << " " << written_sz[i] << endl;
   }

   for(int i = 0; i < OPEN_FILES_MAX; i++)
   {
     cout << "openning file for read: " << files[i];
     fd[i] = FileOpen(files[i], 0);
     cout << "got fd " << fd[i] << endl;
   }

   int read_sz[OPEN_FILES_MAX] = { 0 };
   for(int i = 0; i < 100; i++)
   {
     index = rand() % 8;
     sz = rand() % limit;
     bufer = new char[sz];
     this_sz = FileRead(index, bufer, sz);
     read_sz[index] += this_sz;
     //printbuf(bufer, this_sz);
     delete [] bufer;
   }

   for(int i = 0; i < OPEN_FILES_MAX; i++)
   {
     FileClose(i);
     cout << ref_sz[i] << " " << read_sz[i] << endl;
   }
   TFile  info;
   if ( FileFindFirst ( &info ) )
    do 
    { 
      printf ( "%s %d\n", info . m_FileName, info . m_FileSize );
    } while ( FileFindNext ( &info ) );




    memset(ref_sz, 0, sizeof(int) * 8);
    for(int i = 0; i < OPEN_FILES_MAX; i++)
   {
     randFN(files[i]);
     cout << "openning file for write: " << files[i];
     fd[i] = FileOpen(files[i], 1);
     cout << "got fd " << fd[i] << endl;
   }
   for(int i = 0; i < 100; i++)
   {
     index = rand() % 8;
     sz = rand() % limit;
     bufer = new char[sz];
     randContent(bufer, sz);
     ref_sz[index] += sz;
     written_sz[index] += FileWrite(index, bufer, sz);
     delete [] bufer;
   }

   for(int i = 0; i < OPEN_FILES_MAX; i++)
   {
     FileClose(i);
     cout << ref_sz[i] << " " << written_sz[i] << endl;
   }

   for(int i = 0; i < OPEN_FILES_MAX; i++)
   {
     cout << "openning file for read: " << files[i];
     fd[i] = FileOpen(files[i], 0);
     cout << "got fd " << fd[i] << endl;
   }
   memset(read_sz, 0, 8*4);
   for(int i = 0; i < 100; i++)
   {
     index = rand() % 8;
     sz = rand() % limit;
     bufer = new char[sz];
     this_sz = FileRead(index, bufer, sz);
     read_sz[index] += this_sz;
     //printbuf(bufer, this_sz);
     delete [] bufer;
   }
   cout << "nonexisting: " << FileOpen("hlupak", 0) << endl;
   FsUmount();
   doneDisk(dev);
   return 0;
 }
#endif