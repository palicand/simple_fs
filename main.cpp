#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace std;
#define FILENAME_LEN_MAX    28
#define DIR_ENTRIES_MAX     128
#define OPEN_FILES_MAX      8
#define SECTOR_SIZE         512
#define DEVICE_SIZE_MAX     ( 1024 * 1024 * 1024 )
#define DEVICE_SIZE_MIN     ( 8 * 1024 * 1024 )
#define EOC 0xFFFFFFFF
#define SYS 0xFFFFFFFE
#define SECTORS_PER_BLOCK 8
#define BLOCK_SIZE SECTORS_PER_BLOCK * SECTOR_SIZE
#define MAGIC_NUMBER 42
#define __DEBUG__
//#define __CONVERT__
//#define __DIR_TEST__
#define __FILE_FIND_DEBUG__
#define __CREATE_FILE_DEBUG__

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

 struct TBlock
 {
  unsigned char block[BLOCK_SIZE];
 };

struct TEntry 
{
  unsigned int size;
  unsigned int start;
  char filename[FILENAME_LEN_MAX+1];
  bool opened;
};

 struct TFat
 {
//kazdy blok ma v sebe odkaz na dalsi blok filu, 0, ak je volny a EOC ak sa jedna o posledny sektor
  unsigned int magic_number;
  unsigned int allocated_size;
  unsigned int * blocks;
  unsigned int dir_start;
  unsigned int data_start;
  unsigned int files_total;
  TBlkDev blockDevice;
  TBlock * buffer;
};

struct TDir
{
  TEntry files[DIR_ENTRIES_MAX];
  unsigned int files_total;
};

struct TFileBuffers
{
  TBlock buffers[OPEN_FILES_MAX];
  int free_buffers[OPEN_FILES_MAX];
};
TFat* current_table;
TDir* current_dir;
TFileBuffers buffers;
#endif /* __PROGTEST__ */
int FsCreate(struct TBlkDev * dev);
int     FsMount( struct TBlkDev * dev );
int     FsUmount( void );

int     FileOpen( const char* fileName, int writeMode );
int     FileRead( int fd,void  * buffer, int   len );
int     FileWrite           ( int              fd,   const void     * buffer,  int              len );
int     FileClose           ( int              fd ); 
int     FileDelete          ( const char     * fileName );
int     FileFindFirst       ( struct TFile   * info );
int     FileFindNext        ( struct TFile   * info );
int     FileSize            ( const char     * fileName );


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

#define DISK_SECTORS DEVICE_SIZE_MIN/ 512
static FILE  * g_Fp = NULL;

//-------------------------------------------------------------------------------------------------
/** Sample sector reading function. The function will be called by your fs driver implementation.
 * Notice, the function is not called directly. Instead, the function will be invoked indirectly
 * through function pointer in the TBlkDev structure.
 */
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

   memset    ( buffer, 0, sizeof ( buffer ) );

   g_Fp = fopen ( "/tmp/disk_content", "w+b" );
   if ( ! g_Fp ) return NULL;

   for ( i = 0; i < DISK_SECTORS; i ++ )
    if ( fwrite ( buffer, sizeof ( buffer ), 1, g_Fp ) != 1 )
     return NULL;

   res              = new TBlkDev;
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
   int        i;
   char       fn[100];

   g_Fp = fopen ( "/tmp/disk_content", "r+b" );
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
int main ( void )
 {
   TBlkDev * dev;
   int       i, fd, retCode;
   char      buffer [SECTOR_SIZE];
   TFile     info;

   /* create the disk before we use it
    */
   dev = createDisk ();
   /* The disk is ready at this moment. Your FS-related functions may be executed,
    * the disk backend is ready.
    *
    * First, try to create the filesystem
    */
    FsCreate(dev);
    retCode = FsMount(dev);
    printf("Mount: %d\n", retCode);
    retCode = FileOpen("test.txt", 0);
    printf("open 1, %d\n", retCode);
    retCode = FileOpen("test2.txt", 1);
    printf("open 2, %d\n", retCode);
    retCode = FileOpen("test3.txt", 0);
    printf("open 2, %d\n", retCode);
    doneDisk(dev);

   return 0;
 }
#endif
//manipulacia s FATkou
void init_fat(TFat* table, const TBlkDev* dev);
void write_fat(TFat* table);
void write_fat_info(TFat* table);
void destroy_fat(TFat* table) { delete [] table->blocks; delete table->buffer; }
bool read_fat(TFat* table, const TBlkDev* dev);
bool read_fat_info(TFat* fat, const TBlkDev* dev, TBlock* buffer = NULL);
//manipulacia s blokom
//naplnime block datami z data, velkost data je size, size nesmie byt vacsi ako BLOCK_SIZE
void fill_block(TBlock* block, const unsigned int* data, int size);
//naplnime block charom
void fill_block(TBlock* block, const unsigned char* data, int size);
//zapisanie bloku src od sektoru start_sector na device dev
void write_block(const TBlock* src, int start_sector, const TBlkDev* dev);
void read_block(TBlock* dest, int start_sector, const TBlkDev* dev);
void init_block(TBlock* block) { memset(block->block, 0, sizeof(unsigned char)* BLOCK_SIZE); }
//pomocne blbosti
//prevod uintu src na pole 4 unsigned charov, aby sme to mohli zapisat - pouzitie v zapise fatky; format little endian
void intToUChar(unsigned char chunk[4], unsigned int src)
{
  for(int i = 0; i < 4; i++)
  {
    chunk[i] = (unsigned char) src;
    src >>= 8;
  }
}

int uCharToUint(unsigned char chunk[4])
{
  int num = 0;
  for(int i = 3; i >=0; i--)
  {
    num += chunk[i];
    num <<= 8;
  }
  return num;
}

void init_dir(TDir* dir);
void write_dir(const TDir* root, const TBlkDev* dev, int sector, TBlock* buffer);
bool read_dir(TDir* root, TFat* fat);

//pokusi sa najst subor a vrati jeho fd, v pripade, ze ho nenajde, vrati -1
int find_file(const char* filename);

int create_file(const char* filename);

int find_free_fd(const char* filename, int start = 0);
int find_free_block(int start = 0);

 int FsCreate(struct TBlkDev * dev)
{
  TFat* table = new TFat;
  init_fat(table, dev);
  write_fat(table);
  #ifdef __DEBUG__
  printf("\n%u;%u;%u\n", sizeof(TEntry), sizeof(TDir), sizeof(TFat));
  #endif
  TDir* root = new TDir;

  #ifdef __DEBUG__
  printf("\n%u %u\n", table->dir_start, table->data_start);
  printf("%d, %d, %d, %d\n", dev->m_Sectors, SECTORS_PER_BLOCK, sizeof(unsigned int), BLOCK_SIZE);
  #endif

  init_dir(root);
  write_dir(root, &table->blockDevice, table->dir_start, table->buffer);
  destroy_fat(table);
  delete table;
  delete root;
  return 1;
}

int FsMount(TBlkDev* dev)
{
  #ifdef __DEBUG__
  printf("mounting\n");
  #endif
  TFat * table = new TFat;
  if(!read_fat(table, dev))
    return 0;
  //pretoze sme pri nacitavani FATky zmenili umiestnenie bufferu a table, musime zapisat ich adresu na disk
  write_fat_info(table);
  #ifdef __DEBUG__
  printf("allocating dir\n");
  #endif
  TDir * root = new TDir;
  if(!read_dir(root, table))
    return 0;
  current_table = table;
  current_dir = root;
  return 1;
}

int FsUmount()
{
  write_fat(current_table);
  write_dir(current_dir, &current_table->blockDevice, current_table->dir_start, current_table->buffer);
  close_files(current_dir);
  destroy_fat(current_table);
  current_table = NULL;
  current_dir = NULL;
}

int FileOpen(const char* filename, int writeMode)
{
  if(writeMode != 0 && writeMode != 1)
    return -1;
  int fd = find_file(filename);
  #ifdef __FILE_FIND_DEBUG__
  printf("file found on %d\n", fd);
  #endif
  //subor sa nenasiel
  if(fd == -1)
  {
    //subor otvarany pre citanie neexistuje, vraciame chybu
    if(writeMode == 1)
      return fd;
    fd = create_file(filename);
  }
  current_dir->files[fd].opened = true;
  return fd;
}

void init_fat(TFat* table, const TBlkDev* dev)
{
  memset(table, 0, sizeof(*table));
  table->magic_number = MAGIC_NUMBER;
  table->allocated_size = dev->m_Sectors / SECTORS_PER_BLOCK;
  table->blocks = new unsigned int[table->allocated_size]();
  table->blockDevice = *dev;
  // je mi jasne, ze by sa to dalo zapisat do jedneho vyrazu, ale z nejakeho dovodu mi to strasne blblo a vyhadzovalo nezmyselne cislo, takze som vypocet dir_start radsej rozdelil
  table->dir_start = sizeof(unsigned int) * table->blockDevice.m_Sectors;
  table->dir_start /= BLOCK_SIZE;
  table->dir_start += SECTORS_PER_BLOCK;
  table->data_start = table->dir_start + 2*SECTORS_PER_BLOCK;
  // test
  table->buffer = new TBlock;
  table->files_total = 0;
  for(unsigned i = 0; i < table->data_start / SECTORS_PER_BLOCK; i++)
  {
    table->blocks[i] = SYS;
  }
}

void write_fat(TFat* table)
{
  write_fat_info(table);
  int start_sector = SECTORS_PER_BLOCK;
  int size_to_write = BLOCK_SIZE / 4;
  //tabulka sektorov
  for(unsigned int i = 0; i < table->allocated_size; i += size_to_write)
  {
    init_block(table->buffer);
    if((i + BLOCK_SIZE / 4) > table->allocated_size)
      size_to_write = table->allocated_size - i;
    #ifdef __DEBUG__
    printf("i:%u, size_to_write:%d\n", i, size_to_write);
    #endif
    fill_block(table->buffer, table->blocks + i, size_to_write);
    write_block(table->buffer, start_sector, &table->blockDevice);
    start_sector += SECTORS_PER_BLOCK;
  }
}

void write_fat_info(TFat* table)
{
  init_block(table->buffer);
  memcpy(table->buffer, (void*) table, sizeof(TFat));
  write_block(table->buffer, 0, &table->blockDevice);
}

void fill_block(TBlock* block, const unsigned int* data, int size)
{
  unsigned char chunk[4];
  int start_pos = 0;
  for(int i = 0; i < size; i++)
  {
    intToUChar(chunk, data[i]);
    #ifdef __CONVERT__
    printf("int:%u, byte:%x %x %x %x\n", data[i], chunk[0], chunk[1], chunk[2], chunk[3]);
    #endif
    for(int j = 0; j < 4; j++)
      block->block[start_pos + j] = chunk[j];
    start_pos += 4;
  }
}


void write_block(const TBlock* src, int start_sector, const TBlkDev* dev)
{
  #ifdef __DEBUG__
  printf("writing from sector %d to sector %d, total sectors: %d\n", start_sector, start_sector + SECTORS_PER_BLOCK, dev->m_Sectors);
  #endif
  dev->m_Write(start_sector, src->block, SECTORS_PER_BLOCK);
}

void read_block(TBlock* dest, int start_sector, const TBlkDev* dev)
{
  #ifdef __DEBUG__
  printf("reading from sector %d to sector %d, total sectors: %d\n", start_sector, start_sector + SECTORS_PER_BLOCK, dev->m_Sectors);
  #endif
  dev->m_Read(start_sector, dest, SECTORS_PER_BLOCK);
}

void init_dir(TDir* dir)
{
  dir->files_total = 0;
  for(int i = 0; i < DIR_ENTRIES_MAX; i++)
  {
    dir->files[i].size = -1;
    dir->files[i].start = -1;
    #ifdef __DIR_TEST__
    dir->files[i].start = i;
    #endif
    memset(dir->files[i].filename, '\0', (FILENAME_LEN_MAX + 1) * sizeof(char));
  }
}

void write_dir(const TDir* dir, const TBlkDev* dev, int sector, TBlock* buffer)
{
  int i = 0;
  int size_to_copy = BLOCK_SIZE / sizeof(TEntry);
  while(i < DIR_ENTRIES_MAX)
  {
    init_block(buffer);
    #ifdef __DEBUG__
    printf("copying from entry %d to entry %d\n", i, size_to_copy);
    #endif
    memcpy((void *)buffer->block, (void*)(dir->files + i), size_to_copy * sizeof(TEntry));
    write_block(buffer, sector, dev);
    i += size_to_copy;
    size_to_copy = DIR_ENTRIES_MAX - size_to_copy;
    sector += SECTORS_PER_BLOCK;
  }
}

bool read_fat_info(TFat* fat, const TBlkDev* dev, TBlock* buffer)
{
  bool now_allocated = false;
  if(buffer == NULL)
  {
    buffer = new TBlock;
    now_allocated = true;
  }
  read_block(buffer, 0, dev);
  memcpy(fat, buffer, sizeof(TFat));
  #ifdef __DEBUG__
  printf("mn: %u, as: %u, dir_s: %u, dat_s: %u\n", fat->magic_number, fat->allocated_size, fat->dir_start, fat->data_start);
  #endif
  if(fat->magic_number != MAGIC_NUMBER)
    return false;
  if(now_allocated)
  {
    fat->buffer = buffer;
    fat->blocks = new unsigned int[fat->allocated_size];
  }
  return true;
}



bool read_fat(TFat* fat, const TBlkDev* dev)
{

  //nacitame informacie
  if(!read_fat_info(fat, dev))
    return false;
  #ifdef __DEBUG__
  printf("fat info read\n");
  #endif
  int start_sector = SECTORS_PER_BLOCK;
  //i indexuje buffer, j indexuje fat->blocks
  int i = 0, j = 0;
  unsigned char chunk[sizeof(unsigned int)];
  //nacitame samotnu tabulku
  while(start_sector < fat->dir_start)
  {
    memset(fat->buffer->block, 0, BLOCK_SIZE);
    read_block(fat->buffer, start_sector, dev);
    start_sector += SECTORS_PER_BLOCK;
    for(i = 0; i < BLOCK_SIZE; i += sizeof(unsigned int))
    {
      memcpy(chunk, fat->buffer->block + i, sizeof(unsigned int));
      fat->blocks[j] = uCharToUint(chunk);
      j++;
    }
  }
  return true;
}

bool read_dir(TDir* dir, TFat* table)
{
  int i = 0;
  int start_sector = table->dir_start;
  int size_to_copy = BLOCK_SIZE / sizeof(TEntry);
  while(i < DIR_ENTRIES_MAX)
  {
    init_block(table->buffer);
    read_block(table->buffer, start_sector, &table->blockDevice);
    memcpy(dir->files + i, table->buffer, size_to_copy * sizeof(TEntry));
    i += size_to_copy;
    size_to_copy = DIR_ENTRIES_MAX - size_to_copy; 
    start_sector += SECTORS_PER_BLOCK;
  }
  #ifdef __DIR_TEST__
    for(i = 0; i < DIR_ENTRIES_MAX; i++)
    {
      printf("%u\n", dir->files[i].start);
    }
  #endif
  return true;
}

int find_file(const char* file_name)
{
  int i = 0;
  while(i < DIR_ENTRIES_MAX && strcmp(file_name, current_dir->files[i].filename) != 0)
  {
    i++;
  }
  #ifdef __FILE_FIND_DEBUG____
  printf("file %s has fd %d", file_name, i);
  #endif
  if(i == DIR_ENTRIES_MAX)
    return -1;
  else
    return i;
}

int create_file(const char* filename)
{
  #ifdef __CREATE_FILE_DEBUG__
  printf("creating file\n");
  #endif
  if(current_dir->files_total == DIR_ENTRIES_MAX)
    return -1;
  int fd = find_free_fd(filename);
  #ifdef __FILE_FIND_DEBUG__
  printf("fd: %d\n", fd);
  #endif
  int start_block = find_free_block();
  if(start_block == -1)
    return -1;
  #ifdef __CREATE_FILE_DEBUG__
  printf("block: %d sector:%d\n", start_block, start_block*SECTORS_PER_BLOCK);
  #endif

  //kedze subor nema ziaden obsah zatial, bude koncit na svojom startovnom sektore
  current_table->blocks[start_block] = EOC;
  //nastavime sector, ktory dostaneme, ked vynasobime adresu bloku poctom sektorov na blok
  current_dir->files[fd].start = start_block * SECTORS_PER_BLOCK;
  current_dir->files[fd].size = 0;
  strcpy(current_dir->files[fd].filename, filename);
  current_dir->files_total++;
  return fd;
}

int find_free_fd(const char* filename, int start)
{
  if(start > DIR_ENTRIES_MAX)
    return -1;
  while(start < DIR_ENTRIES_MAX && current_dir->files[start].size != -1)
  {
    start++;
  }
  if(start == DIR_ENTRIES_MAX)
    return -1;
  return start;
}

int find_free_block(int start)
{
  while(start < current_table->allocated_size && current_table->blocks[start] != 0)
  {
    start++;
  }
  if(start == current_table->allocated_size)
    return -1;
  return start;
}