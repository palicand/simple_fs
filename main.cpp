#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <fstream>
#include <ctime>
using namespace std;
#define FILENAME_LEN_MAX    28
#define DIR_ENTRIES_MAX     128
#define OPEN_FILES_MAX      8
#define SECTOR_SIZE         512
#define DEVICE_SIZE_MAX     ( 1024 * 1024 * 1024 )
#define DEVICE_SIZE_MIN     ( 8 * 1024 * 1024 )
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
#define EOC 0xFFFFFFFF
#define SYS 0xFFFFFFFE
#define SECTORS_PER_BLOCK 8
#define BLOCK_SIZE (SECTORS_PER_BLOCK * SECTOR_SIZE)
#define MAGIC_NUMBER 42
//#define __DEBUG__
/*#define __FILE_FIND_DEBUG__
#define __CREATE_FILE_DEBUG__
#define __FILE_WRITE_DEBUG__
#define __FILE_CLOSE_DEBUG__
#define __FILE_READ_DEBUG__*/
 //#define __ANOTHER_TEST__
 struct TBlock
 {
  unsigned char block[BLOCK_SIZE];
 };

//start oznacuje startovny blok, size je velkost v bytoch
struct TEntry 
{
  int size;
  int start;
  char filename[FILENAME_LEN_MAX+1];
};

 struct TFat
 {
//kazdy blok ma v sebe odkaz na dalsi blok filu, 0, ak je volny a EOC ak sa jedna o posledny sektor
  int magic_number;
  int allocated_size;
  int * blocks;
  int dir_start;
  int data_start;
  int files_total;
  TBlkDev blockDevice;
  TBlock * buffer;

  int mounted;
};

struct TDir
{
  TEntry files[DIR_ENTRIES_MAX];
  int files_total;
};

//ked sa otvori subor, jeho adresa v pamati sa zapise do files, vytvori sa mu novy buffer na jeho pozicii vo files a jeho fd sa zapise do fd[pos_files]
struct TOpenFiles
{
  TBlock* buffers[OPEN_FILES_MAX];
  int fd[OPEN_FILES_MAX];
  int block_to_write[OPEN_FILES_MAX];
  int buff_pos[OPEN_FILES_MAX];
  int mode[OPEN_FILES_MAX];
  int last_file;
};

TFat* current_table;
TDir* current_dir;
TOpenFiles* open_files;
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

#define DISK_SECTORS (40*1024*1024) / 512
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

   g_Fp = fopen ( "disk_content", "w+b" );
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

void randContent(char* buffer, int sz)
{
  for(int i = 0; i < sz; i++)
  {
    char c = rand() % 90 + 38;
    buffer[i] = c;
  }
}

void randFN(char* src)
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
int main ( void )
 {
   TBlkDev * dev;
   int       fd, retCode;
   char file[FILENAME_LEN_MAX + 1], prevFile[FILENAME_LEN_MAX + 1];
   TFile info;
   /* create the disk before we use it
    */
     srand(time(0));

   dev = createDisk ();
   /* The disk is ready at this moment. Your FS-related functions may be executed,
    * the disk backend is ready.
    *
    * First, try to create the filesystem
    */
    cout << FsCreate(dev);
  cout << "\n-------------------------------------------------" << endl;
  retCode = FsMount( dev );
  cout << retCode << endl;
  printAllF();
  int sz;
  char* buffer;
  ofstream out("files");
  int i = 0;
  while(i < 10000)
  {
    randFN(file);
    fd = FileOpen(file, 1);
    if(fd != -1)
    {
      strcpy(prevFile, file);
      cout << "open file " << file << ": " << fd << endl;
      sz = rand() % 4096 + 1;
      buffer = new char[sz];
      randContent(buffer, sz);
      FileWrite(fd, buffer, sz);
      FileClose(fd);
      delete [] buffer;
    }
    if(current_dir->files_total > 0)
    {
      if((rand() % 2) == 1)
        cout << "deleting file " << prevFile << ": " << FileDelete(prevFile) << endl;
      else
        out << prevFile << endl;
    }
    i++;
  }
  printAllF();
  retCode = FsUmount( );
  cout << "FsUmount " << retCode << endl;
  doneDisk( dev );
  out.close();
   return 0;
 }
//manipulacia s FATkou
void init_fat(TFat* table, const TBlkDev* dev);
void write_fat(TFat* table);
void write_fat_info(TFat* table);
void destroy_fat(TFat* table) { delete [] table->blocks; delete table->buffer; }
bool read_fat(TFat* table, const TBlkDev* dev);
bool read_fat_info(TFat* fat, const TBlkDev* dev, TBlock* buffer = NULL);
//manipulacia s blokom
//naplnime block datami z data, velkost data je size, size nesmie byt vacsi ako BLOCK_SIZE
void fill_block(TBlock* block, const int* data, int size);
//naplnime block charom
void fill_block(TBlock* block, const unsigned char* data, int size);
//zapisanie bloku src od sektoru start_sector na device dev
int write_block(const TBlock* src, int start_sector, const TBlkDev* dev);
int read_block(TBlock* dest, int start_sector, const TBlkDev* dev);
void init_block(TBlock* block) { memset(block->block, 0, sizeof(unsigned char)* BLOCK_SIZE); }
//pomocne blbosti
//prevod uintu src na pole 4 unsigned charov, aby sme to mohli zapisat - pouzitie v zapise fatky; format little endian
void intToUChar(unsigned char chunk[4], int src)
{
  memcpy(chunk, &src, sizeof(int));
}

int uCharToUint(unsigned char chunk[4])
{
  int num = 0;
  memcpy(&num, chunk, sizeof(*chunk));
  #ifdef __DEBUG__
  cout << num << endl;
  #endif
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

void init_open_files(TOpenFiles* g_of_table);

//prida subor do tabulky otvorenych suborov, v pripade uspechu vracia FD, v pripade neuspechu -1
int add_to_open_files(int fd, TOpenFiles* g_of_table);
void close_files(TOpenFiles* g_of_table);
int get_free_index(const TOpenFiles* g_of_table);

int flush_buffer(int fd, const TOpenFiles* g_of_table);
void remove_from_open_files(int fd, TOpenFiles* g_of_table);

void clear_file(int fd);
void remove_from_table(int fd);
int find_first_from_last();
int get_open_file_index(int fd, const TOpenFiles* g_of_table)
{ 
  int i = 0; 
  while(i < OPEN_FILES_MAX && g_of_table->fd[i] != fd)
    i++;
  if(i == OPEN_FILES_MAX)
    return -1;
  return i;
}

 int FsCreate(struct TBlkDev * dev)
{
  TFat* table = new TFat;
  init_fat(table, dev);
  write_fat(table);
  #ifdef __DEBUG__
  printf("\n%llu;%llu;%llu\n", sizeof(TEntry), sizeof(TDir), sizeof(TFat));
  #endif
  TDir* root = new TDir;

  #ifdef __DEBUG__
  printf("\n%u %u\n", table->dir_start, table->data_start);
  printf("%d, %d, %lu, %d\n", dev->m_Sectors, SECTORS_PER_BLOCK, sizeof(int), BLOCK_SIZE);
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
  {
    delete table;
    return 0;
  }
  if(table->mounted == 1)
  {
    delete table;
    return 0;
  }
  table->mounted = 1;
  table->blockDevice = *dev;
  //pretoze sme pri nacitavani FATky zmenili umiestnenie bufferu a table, musime zapisat ich adresu na disk
  write_fat_info(table);
  #ifdef __DEBUG__
  printf("allocating dir\n");
  #endif
  TDir * root = new TDir;
  if(!read_dir(root, table))
  {
    delete table;
    delete root;
    return 0;
  }
  current_table = table;
  current_dir = root;
  open_files = new TOpenFiles;
  init_open_files(open_files);
  return 1;
}

int FsUmount()
{
  if(current_table == NULL)
    return 0;
  if(current_table->mounted == 0)
    return 0;
  current_table->mounted = 0;
  close_files(open_files);
  write_dir(current_dir, &current_table->blockDevice, current_table->dir_start, current_table->buffer);
  write_fat(current_table);
  destroy_fat(current_table);
  delete current_dir;
  delete current_table;
  delete open_files;
  current_table = NULL;
  current_dir = NULL;
  return 1;
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
    if(writeMode == 0)
      return fd;
    fd = create_file(filename);
  }
  else
  {
    if(writeMode == 1)
    {
      clear_file(fd);
    }
  }
  if(fd == -1)
  {
    #ifdef __ANOTHER_TEST__
    cout << "did not create file\n";
    #endif
    return -1;
  }
  int ret = add_to_open_files(fd, open_files);
  open_files->mode[get_open_file_index(fd, open_files)] = writeMode;
  return ret;
}

int FileClose(int fd)
{
  int index = get_open_file_index(fd, open_files);
  #ifdef __ANOTHER_TEST__
  cout << "passed " << fd << "got " <<  index << "while closing " << endl;
  #endif
  if(open_files->fd[index] == -1)
    return -1;
  #ifdef __FILE_CLOSE_DEBUG__
    printf("closing file %d\n", fd);
  #endif
  //vyprazdnime buffer
  if(open_files->mode[index] == 1)
    flush_buffer(fd, open_files);
  //a subor odstranime z tabulky a upraceme
  remove_from_open_files(index, open_files);
  return 1;
}

int FileRead(int fd, void* buffer, int len)
{
  int file_index = get_open_file_index(fd, open_files);
  if(file_index == -1 && open_files->mode[file_index] != 0)
  {
    return 0;
  }
  #ifdef __FILE_READ_DEBUG__
  printf("reading file %d\n", fd);
  #endif
  //inde suboru v tabulke otvorenych suborov
  //spravim si buffer, do ktoreho budem vsetko kopirovat
  /*unsigned char* temp_buf = new unsigned char[len];
  memset(temp_buf, 0, len);*/
  //kde som v konecnom bufferi
  int destPos = 0;
  //velkost, ktoru musim nacitat
  int size_to_read = 0;
  //musime sa dostat na blok, odkial musime citat
  int rel_block = open_files->buff_pos[file_index] / SECTORS_PER_BLOCK;
  rel_block /= SECTOR_SIZE;
  int block = current_dir->files[fd].start;

  //kde presne sme v ramci bloku
  #ifdef __FILE_READ_DEBUG__
  printf("the relative block is: %d\n", rel_block);
  #endif
  for(int i = 0; i < rel_block; i++)
    block = current_table->blocks[block];
  int pos_in_block = open_files->buff_pos[file_index] - rel_block * BLOCK_SIZE;
  //nacitavam, kym je co citat - bud kym nedosiahnem dlzku suboru, alebo kym nenaplnim buffer, ktory mi bol poskytnuty
  while(open_files->buff_pos[file_index] < current_dir->files[fd].size && destPos < len)
  {
    int upper_limit = (BLOCK_SIZE < (current_dir->files[fd].size - rel_block * BLOCK_SIZE)) ? BLOCK_SIZE : (current_dir->files[fd].size - rel_block * BLOCK_SIZE);
    //velkost na citanie je bud na naplnenie buffera velkosti bloku, laebo buffera velkosti suboru, ktorekolvek je mensie
    size_to_read = ((upper_limit - pos_in_block) < (len - destPos)) ? ((upper_limit - pos_in_block)) :  (len - destPos);
    #ifdef __FILE_READ_DEBUG__
    printf("size to read %d from block %d\n", size_to_read, block);
    #endif
    //nacitame blok
    read_block(open_files->buffers[file_index], block * SECTORS_PER_BLOCK, &current_table->blockDevice);
    memcpy((unsigned char*) buffer + destPos, open_files->buffers[file_index]->block + pos_in_block, size_to_read);
    #ifdef __FILE_READ_DEBUG__
    printf("copied from position %d in src buffer to position %d in dest buffer\n", pos_in_block, destPos);
    #endif
    pos_in_block += size_to_read;
    open_files->buff_pos[file_index] += size_to_read;
    destPos += size_to_read;
    if(pos_in_block == BLOCK_SIZE)
    {
      pos_in_block = 0;
      #ifdef __FILE_READ_DEBUG__
      printf("current block %d, next block %d\n", block, current_table->blocks[block]);
      #endif
      block = current_table->blocks[block];
      rel_block++;
    }
  }
  return destPos;
}

int FileWrite(int fd, const void* buffer, int len)
{
  int file_index = get_open_file_index(fd, open_files);
  if(file_index == -1 || open_files->mode[file_index] != 1)
    return 0;
  #ifdef __FILE_WRITE_DEBUG__
  printf("writing file %d\n", fd);
  #endif
  int srcPos = 0;
  int sizeToWrite = 0;
  /*unsigned char* temp_buf = new unsigned char[len];
  memcpy(temp_buf, buffer, len * sizeof(unsigned char));*/
  while(srcPos != len)
  {
    /*musime zistit, kolko zo zdrojoveho bufferu nakopirujeme do nasho, musi to byt velkost, ktora co najviac naplni nas buffer,
    ale zaroven nepreiahneme velkost zdrojoveho... ak je teda velkost zdrojoveho bufferu este dostatocne velka, kopirujem tolko, 
    kolko mi chyba po okraj mojho bufferu (BLOCK_SIZE - buff_pos), ale ak nie je, tak berem proste to, co mi zostava v zdrojovom bufferi
    */
    sizeToWrite = ((BLOCK_SIZE - open_files->buff_pos[file_index]) < (len - srcPos)) ? (BLOCK_SIZE - open_files->buff_pos[file_index]) : (len - srcPos);
    #ifdef __FILE_WRITE_DEBUG__
    printf("will write %d bytes, to bufpos %d from srcpos %d\n", sizeToWrite, open_files.buff_pos[file_index], srcPos);
    #endif
    memcpy(open_files->buffers[file_index]->block + open_files->buff_pos[file_index], (unsigned char*)buffer + srcPos, sizeToWrite);
    open_files->buff_pos[file_index] += sizeToWrite;
    srcPos += sizeToWrite;
    //naplnili sme buffer
    if(open_files->buff_pos[file_index] == BLOCK_SIZE)
    {
      //teraz hu musime zapisat na disk a to na poziciu, na ktorej akurat sme, kedze ta j evzdy prazdna
      int ret;
      if((ret = flush_buffer(fd, open_files)) != SECTORS_PER_BLOCK)
      {
        #ifdef __FILE_WRITE_DEBUG__
        printf("wrote %d bytes while flushing\n", ret);
        #endif
        current_dir->files[fd].size += srcPos;
        return srcPos;
      }
      //dostaneme ziskame novy blok
      if(srcPos == len)
        break;
      int new_block = find_free_block();
      #ifdef __FILE_WRITE_DEBUG__
      printf("got new block %d\n", new_block);
      #endif
      //nie su ziadne volne bloky uz
      if(new_block == -1)
      {
        current_dir->files[fd].size += srcPos;
        return srcPos;
      }
      //na sucastnom bloku nastavime odkaz na dalsi blok, kde file pokracuje
      current_table->blocks[open_files->block_to_write[file_index]] = new_block;
      open_files->block_to_write[file_index] = new_block;
      //do noveho bloku nastavime EOC
      current_table->blocks[open_files->block_to_write[file_index]] = EOC;
      open_files->buff_pos[file_index] = 0;
    }
  }
  //delete [] temp_buf;
  current_dir->files[fd].size += srcPos;
  return srcPos;
}

int FileDelete(const char* fileName )
{
  int fd = find_file(fileName);
  if(fd == -1)
    return 0;
  clear_file(fd);
  remove_from_table(fd);
  current_dir->files_total--;
  return 1;
}

int FileFindFirst(TFile* info)
{
  open_files->last_file = -1;
  int file = find_first_from_last();
  if(file == -1)
  {
    return 0;
  }
  strcpy(info->m_FileName, current_dir->files[file].filename);
  info->m_FileSize = current_dir->files[file].size;
  return 1;
}

int FileFindNext(TFile* info)
{
  int file = find_first_from_last();
  if(file == -1)
    return 0;
  strcpy(info->m_FileName, current_dir->files[file].filename);
  info->m_FileSize = current_dir->files[file].size;
  return 1;
}

int FileSize ( const char     * fileName )
{
  int file = find_file(fileName);
  if(file == -1)
    return -1;
  else return current_dir->files[file].size;
}

void init_fat(TFat* table, const TBlkDev* dev)
{
  memset(table, 0, sizeof(*table));
  table->magic_number = MAGIC_NUMBER;
  table->allocated_size = dev->m_Sectors / SECTORS_PER_BLOCK;
  table->blocks = new int[table->allocated_size]();
  table->blockDevice.m_Read = dev->m_Read;
  table->blockDevice.m_Write = dev->m_Write;
  table->blockDevice.m_Sectors = dev->m_Sectors;
  // je mi jasne, ze by sa to dalo zapisat do jedneho vyrazu, ale z nejakeho dovodu mi to strasne blblo a vyhadzovalo nezmyselne cislo, takze som vypocet dir_start radsej rozdelil
  table->dir_start = sizeof(int) * table->blockDevice.m_Sectors;
  table->dir_start /= BLOCK_SIZE;
  table->dir_start += SECTORS_PER_BLOCK;
  table->data_start = table->dir_start + 2*SECTORS_PER_BLOCK;
  // test
  table->buffer = new TBlock;
  init_block(table->buffer);
  table->files_total = 0;
  for(int i = 0; i < table->data_start / SECTORS_PER_BLOCK; i++)
  {
    table->blocks[i] = SYS;
  }
  table->mounted = 0;
}

void write_fat(TFat* table)
{
  write_fat_info(table);
  int start_sector = SECTORS_PER_BLOCK;
  int size_to_write = BLOCK_SIZE / 4;
  //tabulka sektorov
  for(int i = 0; i < table->allocated_size; i += size_to_write)
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

void fill_block(TBlock* block, const int* data, int size)
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


int write_block(const TBlock* src, int start_sector, const TBlkDev* dev)
{
  #ifdef __DEBUG__
  printf("writing from sector %d to sector %d, total sectors: %d\n", start_sector, start_sector + SECTORS_PER_BLOCK, dev->m_Sectors);
  #endif
  return dev->m_Write(start_sector, src->block, SECTORS_PER_BLOCK);
}

int read_block(TBlock* dest, int start_sector, const TBlkDev* dev)
{
  #ifdef __DEBUG__
  printf("reading from sector %d to sector %d, total sectors: %d\n", start_sector, start_sector + SECTORS_PER_BLOCK, dev->m_Sectors);
  #endif
  return dev->m_Read(start_sector, dest, SECTORS_PER_BLOCK);
}

void init_dir(TDir* dir)
{
  memset(dir, 0, sizeof(*dir));
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
  buffer = new TBlock;
  init_block(buffer);
  read_block(buffer, 0, dev);
  memcpy(fat, buffer, sizeof(TFat));
  #ifdef __DEBUG__
  printf("mn: %u, as: %u, dir_s: %u, dat_s: %u\n", fat->magic_number, fat->allocated_size, fat->dir_start, fat->data_start);
  #endif
  if(fat->magic_number != MAGIC_NUMBER)
    return false;
  fat->buffer = buffer;
  fat->blocks = new int[fat->allocated_size];
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
  unsigned char chunk[sizeof(int)];
  //nacitame samotnu tabulku
  while(start_sector < fat->dir_start)
  {
    memset(fat->buffer->block, 0, BLOCK_SIZE);
    read_block(fat->buffer, start_sector, dev);
    start_sector += SECTORS_PER_BLOCK;
    for(i = 0; i < BLOCK_SIZE; i += sizeof(int))
    {
      memcpy(chunk, fat->buffer->block + i, sizeof(int));
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
  dir->files_total = table->files_total;
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
  printf("creating file, total files: %d\n", current_dir->files_total);
  #endif
  if(current_dir->files_total == DIR_ENTRIES_MAX)
  {
    #ifdef __ANOTHER_TEST__
    cout << "too much files, exiting\n";
    #endif
    return -1;
  }
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
  //nastavime cluster
  current_dir->files[fd].start = start_block;
  current_dir->files[fd].size = 0;
  strcpy(current_dir->files[fd].filename, filename);
  current_dir->files_total++;
  current_table->files_total = current_dir->files_total;
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

void init_open_files(TOpenFiles* g_of_table)
{
  memset(g_of_table->buffers, 0, sizeof(TBlock*) * OPEN_FILES_MAX);
  memset(g_of_table->fd, -1, sizeof(*g_of_table->fd) * OPEN_FILES_MAX);
  memset(g_of_table->block_to_write, -1, sizeof(*g_of_table->block_to_write) * OPEN_FILES_MAX);
  memset(g_of_table->buff_pos, 0, sizeof(*g_of_table->buff_pos) * OPEN_FILES_MAX);
  memset(g_of_table, -1, sizeof(*g_of_table->mode) * OPEN_FILES_MAX);
  open_files->last_file = -1;
}

#ifdef __CREATE_FILE_DEBUG__
void printOTable(const TOpenFiles* g_of_table)
{
  for(int i = 0; i < OPEN_FILES_MAX; i++)
  {
    printf("%d, fd:%d\n", i, g_of_table->fd[i]);
  }
}
#endif

int add_to_open_files(int fd, TOpenFiles* g_of_table)
{
  #ifdef __CREATE_FILE_DEBUG__
  printOTable(g_of_table);
  #endif
  int index = get_free_index(g_of_table);
  #ifdef __CREATE_FILE_DEBUG__
  printf("got index %d\n", index);
  #endif
  //nebolo volne miesto pre otvoreny subor
  if(index == -1)
  {
    printf("no space\n");
    return -1;
  }
  #ifdef __CREATE_FILE_DEBUG__
  printf("adding file %d to table\n", fd);
  #endif
  g_of_table->buffers[index] = new TBlock;
  init_block(g_of_table->buffers[index]);
  g_of_table->fd[index] = fd;
  g_of_table->block_to_write[index] = current_dir->files[fd].start;
  #ifdef __CREATE_FILE_DEBUG__
  printf("after add\n");
  printOTable(g_of_table);
  #endif
  return fd;
}


int get_free_index(const TOpenFiles* g_of_table)
{
  int i = 0;
  while(i < OPEN_FILES_MAX && g_of_table->fd[i] != -1)
    i++;
  if(i == OPEN_FILES_MAX)
    return -1;
  return i;
}

void close_files(TOpenFiles* g_of_table)
{
  for(int i = 0; i < OPEN_FILES_MAX; i++)
  {
    #ifdef __ANOTHER_TEST__
    cout << g_of_table->fd[i] << endl;
    #endif
    if(g_of_table->fd[i] != -1)
    {
      #ifdef __FILE_CLOSE_DEBUG__
      printf("closing file %d\n", g_of_table->fd[i]);
      #endif
      #ifdef __ANOTHER_TEST__
      cout << "found open file while unmounting, closing\n";
      #endif
      FileClose(g_of_table->fd[i]);
    }
  }
}

int flush_buffer(int fd, const TOpenFiles* g_of_table)
{
  int file_index = get_open_file_index(fd, g_of_table);
  #ifdef __FILE_WRITE_DEBUG__
  printf("flushing buffer of %d in sector %lu\n", fd, g_of_table->block_to_write[file_index]*SECTORS_PER_BLOCK);
  #endif
  int size_written = write_block(g_of_table->buffers[file_index], g_of_table->block_to_write[file_index]*SECTORS_PER_BLOCK, &current_table->blockDevice);
  init_block(g_of_table->buffers[file_index]);
  return size_written;
}

void remove_from_open_files(int file_index, TOpenFiles* g_of_table)
{
  #ifdef __FILE_CLOSE_DEBUG__
  printf("removing file %d from g_of_table, position %d\n", open_files.fd[file_index], file_index);
  #endif
  g_of_table->block_to_write[file_index] = -1;
  g_of_table->buff_pos[file_index] = 0;
  g_of_table->fd[file_index] = -1;
  delete g_of_table->buffers[file_index];
  g_of_table->buffers[file_index] = NULL;
}

void clear_file(int fd)
{
  int current_block = current_dir->files[fd].start;
  int next_block = current_table->blocks[current_block];
  while(next_block != (int)EOC)
  {
    current_table->blocks[current_block] = 0;
    current_block = next_block;
    next_block = current_table->blocks[current_block];
  }
  current_table->blocks[current_block] = 0;
  current_table->blocks[current_dir->files[fd].start] = EOC;
}

void remove_from_table(int fd)
{
  //pred volanim tejto funkcie sme volali cear_file, takze zostava uz len zaciatocny blok, ten vynulujeme
  current_table->blocks[current_dir->files[fd].start] = 0;
  current_dir->files[fd].size = -1;
  current_dir->files[fd].start = -1;
  memset(current_dir->files[fd].filename, '\0', (FILENAME_LEN_MAX + 1) * sizeof(char));
}

int find_first_from_last()
{
  int local = open_files->last_file;
  local++;
  while(local < DIR_ENTRIES_MAX && current_dir->files[local].start == -1)
    local++;
  if(local == DIR_ENTRIES_MAX)
    return -1;
  open_files->last_file = local;
  return open_files->last_file;
}

int segtest(int sector)
{
  TBlock* temp = new TBlock;
  init_block(temp);
  write_block(temp, sector, &current_table->blockDevice);
  delete temp;
  return 1;
}