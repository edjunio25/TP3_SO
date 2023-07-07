#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/types.h>
#include <stddef.h>

//Buscar o tamanho do arquivo fname
int fileSize(const char *filename){
    FILE *file = fopen(filename, "r");
    int tam, prev;
    prev=ftell(file);
    fseek(file, 0L, SEEK_END);
    tam=ftell(file);
    fseek(file,prev,SEEK_SET);
    fclose(file);
    return tam;
}

int getNumBlocks(const char *file, int blockSize){
    int numBlocks =  fileSize(file)/blockSize;
    return numBlocks;
}

/* Build a new filesystem image in =fname (the file =fname should be present
 * in the OS's filesystem).  The new filesystem should use =blocksize as its
 * block size; the number of blocks in the filesystem will be automatically
 * computed from the file size.  The filesystem will be initialized with an
 * empty root directory.  This function returns NULL on error and sets errno
 * to the appropriate error code.  If the block size is smaller than
 * MIN_BLOCK_SIZE bytes, then the format fails and the function sets errno to
 * EINVAL.  If there is insufficient space to store MIN_BLOCK_COUNT blocks in
 * =fname, then the function fails and sets errno to ENOSPC. */
struct superblock *fs_format(const char *fname, uint64_t blocksize) {
    // Verificar se o tamanho do bloco é válido
    if (blocksize < MIN_BLOCK_SIZE) {
        errno = EINVAL;
        return NULL;
    }

    int fd = open(fname, O_RDWR, 0666);

    if (fd == -1) {
        errno = EBADF;
        return NULL;
    }

    struct superblock *superbloco = malloc(sizeof *superbloco);
    
    superbloco->magic = 0xdcc605f5; /* 0xdcc605f5 */
	superbloco->blks = getNumBlocks(fname,blocksize); /* number of blocks in the filesystem */

    //espaço insuficiente em fname para os blocos
    if(superbloco->blks < MIN_BLOCK_COUNT) { 
		errno = ENOSPC;
		free(superbloco);
		return NULL;
	}
	
    superbloco->blksz = blocksize; /* block size (bytes) */
	superbloco->freeblks = superbloco->blks-3; /* number of free blocks in the filesystem */
	superbloco->freelist = 3; /* pointer to free block list */ //Três para acomodar o superbloco, inode raiz e nodeinfo raiz
	superbloco->root = 1; /* pointer to root directory's inode */
	superbloco->fd = fd; /* file descriptor for the filesystem image */

    write(superbloco->fd, superbloco, superbloco->blksz);

    if (superbloco->fd == -1) {
        // Erro ao abrir o arquivo
        errno = EBADF;
		free(superbloco);
        return NULL;
    }

    //definindo inode raiz
    struct inode *rootBloodyRoot = malloc(blocksize);

    rootBloodyRoot->mode = IMDIR;
	rootBloodyRoot->parent = 1;
	rootBloodyRoot->meta = 2;
    
    write(superbloco->fd, rootBloodyRoot, superbloco->blksz);
	free(rootBloodyRoot);

    struct nodeinfo *rootInfo = malloc(blocksize);

    rootInfo->size = 0;
	strcpy(rootInfo->name, "/");
    
    write(superbloco->fd, rootInfo, superbloco->blksz);
    free(rootInfo);

    struct freepage *paginaLivre = malloc(blocksize);

    for(uint64_t i=3; i< superbloco->blks;i++) {
		paginaLivre->next = (i+1==superbloco->blks)?0:i+1;
		paginaLivre->count = 0;
        lseek(superbloco->fd, (i*superbloco->blksz), SEEK_SET);
	    write(superbloco->fd, paginaLivre, superbloco->blksz);
	}

    free(paginaLivre);

    return(superbloco);

}


/* Open the filesystem in =fname and return its superblock.  Returns NULL on
 * error, and sets errno accordingly.  If =fname does not contain a
 * 0xdcc605fs, then errno is set to EBADF. */
struct superblock * fs_open(const char *fname){

    struct superblock *superAux = malloc(sizeof *superAux);

    FILE *arquivo = fopen(fname, "rb");

    //checa se o arquivo for nulo, caso for sai da função e retorna erro
    if (arquivo == NULL) {
        errno = EBADF;
        free(superAux);
        return NULL;
    }

    //checa se arquivo já está aberto A FAZER
    if(flock(open(fname, O_RDWR), LOCK_EX | LOCK_NB) == -1){
		errno = EBUSY;
		return NULL;
	}

    fread(superAux, sizeof(struct superblock), 1, arquivo);
    superAux->fd = read(superAux->fd, (void *) superAux, sizeof(struct superblock));

    //checa se o arquivo contem o marcador 0xdcc605f5
    if (superAux->magic != 0xdcc605f5) {
        errno = EBADF;
        fclose(arquivo);
        free(superAux);
        return NULL;
    }

    return superAux;
}

/* Close the filesystem pointed to by =sb.  Returns zero on success and a
 * negative number on error.  If there is an error, all resources are freed
 * and errno is set appropriately. */
int fs_close(struct superblock *sb){
    if(sb == NULL)
		return -1;

	if(sb->magic != 0xdcc605f5) {
		errno = EBADF;
		return -1;
	}

	flock(sb->fd, LOCK_UN);
	close(sb->fd);
	free(sb);
	return 0;
}

/* Get a free block in the filesystem.  This block shall be removed from the
 * list of free blocks in the filesystem.  If there are no free blocks, zero
 * is returned.  If an error occurs, (uint64_t)-1 is returned and errno is set
 * appropriately. */
uint64_t fs_get_block(struct superblock *sb){
    if(sb == NULL){
        errno = EBADF;
		return (uint64_t) -1;
    }

    if(sb->magic != 0xdcc605f5){
		errno = EBADF;
		return (uint64_t) -1;
	}

    if(sb->freeblks == 0){ 
        return (uint64_t) 0;
    }

    struct freepage *pagina = malloc (sb->blksz);

    lseek(sb->fd, sb->freelist * sb->blksz, SEEK_SET);

	if(read(sb->fd, pagina, sb->blksz) == -1){
        free(pagina);
        return (uint64_t) 0;
    }

    uint64_t bloco = sb->freelist;

    sb->freelist = pagina->next;

    sb->freeblks--;

    lseek(sb->fd, 0 * sb->blksz, SEEK_SET);
	write(sb->fd, sb, sb->blksz);
    
	free(pagina);

	return bloco;
    
}

/* Put =block back into the filesystem as a free block.  Returns zero on
 * success or a negative value on error.  If there is an error, errno is set
 * accordingly. */
int fs_put_block(struct superblock *sb, uint64_t block){
	if(sb == NULL){
        errno = EBADF;
		return (uint64_t) -1;
    }

    if(sb->magic != 0xdcc605f5){
		errno = EBADF;
		return (uint64_t) -1;
	}

    if(sb->freeblks == 0){ 
        return (uint64_t) 0;
    }
	
	struct freepage *pagina = malloc(sb->blksz);

	pagina->next = sb->freelist;
	pagina->count = 0;
	sb->freelist = block;
	sb->freeblks++;

    lseek(sb->fd, 0 * sb->blksz, SEEK_SET);
    write(sb->fd, sb, sb->blksz);

    lseek(sb->fd, block * sb->blksz, SEEK_SET);
	write(sb->fd, pagina, sb->blksz);

	free(pagina);

	return 0;
}

int fs_write_file(struct superblock *sb, const char *fname, char *buf, size_t cnt){

}

ssize_t fs_read_file(struct superblock *sb, const char *fname, char *buf, size_t bufsz){

}

int fs_unlink(struct superblock *sb, const char *fname){

}

int fs_mkdir(struct superblock *sb, const char *dname){
    
}

int fs_rmdir(struct superblock *sb, const char *dname){
    
}

char * fs_list_dir(struct superblock *sb, const char *dname){
    
}