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

//MODIFIQUE OS NOMES DAS CONSTANTES!!!

#define MAX_NAME 100 // Max files' name
#define MAX_SUBFOLDERS 100 // Max number of subfolders in a path
#define MAX_FILE_SIZE 5000 // Max file size (in blocks)
#define MAX_PATH_NAME 4000// Max length of a path's name
#define EPS 1e-6 // Epsilon to compare float numbers

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
    int i, j, k, current_allocated_blocks, found;
    int num_elements_in_path, num_new_blocks, new_file;
    uint64_t blocks[MAX_FILE_SIZE], parent_in_b, parent_info_b;
    
    char files[MAX_SUBFOLDERS][MAX_NAME];
    char *token;
    char *name;
    
    struct inode *in, *in2, *parent_in;
    struct nodeinfo *parent_info, *info, *info2;

    in = (struct inode*) malloc(sb->blksz);
    in2 = (struct inode*) malloc(sb->blksz);
    parent_in = (struct inode*) malloc(sb->blksz);
    info = (struct nodeinfo*) malloc(sb->blksz);
    info2 = (struct nodeinfo*) malloc(sb->blksz);
    parent_info = (struct nodeinfo*) malloc(sb->blksz);

    name = (char*) malloc(MAX_PATH_NAME*sizeof(char));
    strcpy(name, fname);

    // Separa as subpastas em um vetor de strings
    i = 0;
    token = strtok(name, "/"); // Raiz
    while(token != NULL){
        strcpy(files[i], token);
        token = strtok(NULL, "/");
        i++;
    }
    num_elements_in_path = i;

    // Nodeinfo da raiz
    lseek(sb->fd, sb->blksz, SEEK_SET);
    read(sb->fd, info, sb->blksz);

    // iNode da raiz
    lseek(sb->fd, 2*sb->blksz, SEEK_SET);
    read(sb->fd, in, sb->blksz);

    copy_inode(parent_in, in, info);
    copy_nodeinfo(parent_info, info);

    parent_info_b = 1;
    parent_in_b = 2;

    blocks[0] = 1;
    blocks[1] = 2;

    new_file = 0;
    // Percorre cada pasta no caminho, até chegar ao arquivo, se ele existir
    for(j = 0; j < num_elements_in_path; j++){        
        // Verifica cada elemento dentro do diretório atual
        while(1){
            // Verifica se o elemento está no iNode atual
            found = 0;

            for(k = 0; k < info->size; k++){
                // iNode de um arquivo
                lseek(sb->fd, in->links[k]*sb->blksz, SEEK_SET);
                read(sb->fd, in2, sb->blksz);
            
                // Verifica se estamos em um iNode filho
                if(in2->mode == IMCHILD){
                    // Pula para o primeiro iNode
                    lseek(sb->fd, in2->parent*sb->blksz, SEEK_SET);
                    read(sb->fd, in2, sb->blksz);
                }
                
                // Obtém as informações do nó do arquivo
                lseek(sb->fd, in2->meta*sb->blksz, SEEK_SET);
                read(sb->fd, info2, sb->blksz);

                if(strcmp(info2->name, files[j]) == 0){
                    found = 1;
                    break;
                }
            }

            if(found){
                // A subpasta ou arquivo foi encontrado no iNode atual
                if(j == (num_elements_in_path-1)){
                    // O arquivo foi encontrado
                    info->size++;
                    blocks[0] = in2->meta;
                    blocks[1] = in->links[k];
                    new_file = 0;
                }else{
                    copy_inode(parent_in, in2, info2);
                    copy_nodeinfo(parent_info, info2);

                    parent_in_b = in->links[k];
                    parent_info_b = in2->meta;
                }
                break;
            }else{
                // A subpasta ou arquivo não foi encontrado no iNode atual
                
                if(j == (num_elements_in_path-1)){
                    // O arquivo não existe. Cria um novo arquivo

                    // Novo nodeinfo
                    blocks[0] = fs_get_block(sb);
                    strcpy(info2->name, files[j]);
                    info2->size = sb->blksz - 20;

                    // Novo iNode
                    blocks[1] = fs_get_block(sb);
                    in2->mode = IMREG;
                    in2->parent = blocks[1];
                    in2->meta = blocks[0];
                    in2->next = 0;

                    new_file = 1;
                    break;
                }else if(in->next == 0){
                    // Nenhum diretório encontrado
                    errno = ENOENT;
                    return -1;
                }
            }

            // Pula para o próximo iNode
            lseek(sb->fd, in->next*sb->blksz, SEEK_SET);
            read(sb->fd, in, sb->blksz);
        }
        // Pula para o próximo diretório
        // info = info2;

        copy_inode(in, in2, info2);
        copy_nodeinfo(info, info2);

    }
    // blocks[0] = Contém o bloco do nodeinfo
    // blocks[1] = Contém o bloco do primeiro iNode

    if(new_file){
        parent_in->links[parent_info->size] = blocks[1];
        parent_info->size++;
    
        // Número de blocos necessários para escrever = buf
        num_new_blocks = ((float)cnt)/(sb->blksz-20.0);
        if(num_new_blocks - (float)(cnt/(sb->blksz-20)) >= EPS){
            num_new_blocks++;
        }

        // num_new_blocks = ceil(((float)cnt)/(sb->blksz-20.0));

        // Obtém novos blocos, se necessário
        for(i = 0; i < num_new_blocks-1; i++){
            blocks[i+2] = fs_get_block(sb);
        }

    }else{
        current_allocated_blocks = ((float)info->size)/(sb->blksz-20.0);
        if(current_allocated_blocks - (float)(info->size/(sb->blksz-20)) >= EPS){
            current_allocated_blocks++;
        }

        num_new_blocks = ((float)cnt)/(sb->blksz-20.0);
        if(num_new_blocks > (float)(cnt/(sb->blksz-20)) ){
            num_new_blocks++;
        }
        
        if(current_allocated_blocks < num_new_blocks || current_allocated_blocks == num_new_blocks){
            i = 2;
            copy_inode(in2, in, info);
            // in2 = in;
            while(in2->next != 0){
                blocks[i] = in->next;
                lseek(sb->fd, blocks[i]*sb->blksz, SEEK_SET);
                read(sb->fd, in2, sb->blksz);
                i++;
            }

            // Obtém o restante dos blocos
            for(i = current_allocated_blocks+1; i <= num_new_blocks; i++){
                blocks[i] = fs_get_block(sb);
            }

        }else{
            // Existem mais iNodes do que o necessário
            i = 2;
            // in2 = in;
            copy_inode(in2, in, info);
            while(in2->next != 0){
                blocks[i] = in->next;
                lseek(sb->fd, blocks[i]*sb->blksz, SEEK_SET);
                read(sb->fd, in2, sb->blksz);
                i++;
            }

            // Libera os blocos em excesso
            for(i = current_allocated_blocks; i > num_new_blocks; i--){
                if(fs_put_block(sb, blocks[i]) != 0){

                    return -1;
                }
            }
        }
    }


    // Atualiza o nodeinfo pai
    lseek(sb->fd, parent_info_b*sb->blksz, SEEK_SET);
    write(sb->fd, parent_info, sb->blksz);


    // Atualiza o iNode pai
    lseek(sb->fd, parent_in_b*sb->blksz, SEEK_SET);
    write(sb->fd, parent_in, sb->blksz);    

    // Escreve o novo nodeinfo do arquivo
    lseek(sb->fd, blocks[0]*sb->blksz, SEEK_SET);
    write(sb->fd, info, sb->blksz);    

    // Escreve o primeiro iNode
    in->mode = IMREG;
    in->parent = blocks[1];
    in->meta = blocks[0];
    if(num_new_blocks == 1){
        in->next = 0;
    }else{
        in->next = blocks[2];
    }

    lseek(sb->fd, blocks[1]*sb->blksz, SEEK_SET);
    write(sb->fd, in, sb->blksz);

    // Escreve os iNodes filhos, se houver algum
    in->mode = IMCHILD;
    in->parent = blocks[1];

    // Escreve o buffer em todos os iNodes, de acordo com o número de blocos no vetor "blocks"
    for(i = 2; i <= num_new_blocks; i++){
        // Escreve o nodeinfo
        in->meta = blocks[i-1];
        if(i < num_new_blocks){
            in->next = blocks[i+1];                    
        }else{
            in->next = 0;
        }
    
        lseek(sb->fd, blocks[i]*sb->blksz, SEEK_SET);
        write(sb->fd, in, sb->blksz);            
    }    
    
    // Atualiza sb
    lseek(sb->fd, 0, SEEK_SET);
    if(write(sb->fd, sb, sb->blksz) < 0){

        return -1;
    }

    free(in);
    free(in2);
    free(parent_in);
    free(info);
    free(info2);
    free(parent_info);

    return 0;
}


ssize_t fs_read_file(struct superblock *sb, const char *fname, char *buf, size_t bufsz){

}


int fs_unlink(struct superblock *sb, const char *fname) {
    int index_i, index_j, index_k, is_found;
    int num_path_elements;
    uint64_t blocks[MAX_FILE_SIZE], parent_inode_block, parent_nodeinfo_block;
    char filenames[MAX_SUBFOLDERS][MAX_NAME];
    char *token;
    char *path;
    
    struct inode *current_inode, *child_inode, *parent_inode;
    struct nodeinfo *current_nodeinfo, *child_nodeinfo, *parent_nodeinfo;

    current_inode = (struct inode*) malloc(sb->blksz);
    child_inode = (struct inode*) malloc(sb->blksz);
    parent_inode = (struct inode*) malloc(sb->blksz);
    current_nodeinfo = (struct nodeinfo*) malloc(sb->blksz);
    child_nodeinfo = (struct nodeinfo*) malloc(sb->blksz);
    parent_nodeinfo = (struct nodeinfo*) malloc(sb->blksz);

    path = (char*) malloc(MAX_PATH_NAME * sizeof(char));
    strcpy(path, fname);

    // Separar as subpastas em um vetor de strings
    index_i = 0;
    token = strtok(path, "/"); // Raiz
    while (token != NULL) {
        strcpy(filenames[index_i], token);
        token = strtok(NULL, "/");
        index_i++;
    }
    num_path_elements = index_i;

    // Inode raiz
    lseek(sb->fd, sb->root * sb->blksz, SEEK_SET);
    read(sb->fd, current_inode, sb->blksz);

    // Nodeinfo raiz
    lseek(sb->fd, sb->blksz, SEEK_SET);
    read(sb->fd, current_nodeinfo, sb->blksz);

    parent_nodeinfo_block = 1; // Nodeinfo raiz
    parent_inode_block = 2; // Inode raiz

    // Percorrer cada pasta no caminho até encontrar o arquivo, se existir
    for (index_j = 0; index_j < num_path_elements; index_j++) {
        // Verificar cada elemento dentro do diretório atual
        while (1) {
            is_found = 0;
            // Verificar se o elemento está no inode atual
            for (index_k = 0; index_k < current_nodeinfo->size; index_k++) {
                // Inode de um arquivo
                lseek(sb->fd, current_inode->links[index_k] * sb->blksz, SEEK_SET);
                read(sb->fd, child_inode, sb->blksz);
            
                // Verificar se estamos em um inode filho
                if (child_inode->mode == IMCHILD) {
                    // Pular para o primeiro inode
                    lseek(sb->fd, child_inode->parent * sb->blksz, SEEK_SET);
                    read(sb->fd, child_inode, sb->blksz);
                }
                
                // Obter o nodeinfo do arquivo
                lseek(sb->fd, child_inode->meta * sb->blksz, SEEK_SET);
                read(sb->fd, child_nodeinfo, sb->blksz);

                if (strcmp(child_nodeinfo->name, filenames[index_j]) == 0) {
                    is_found = 1;
                    break;
                }
            }

            if (is_found) {
                // A subpasta ou arquivo foi encontrado no inode atual
                if (index_j == (num_path_elements - 1)) {
                    blocks[0] = child_inode->meta;
                    blocks[1] = current_inode->links[index_k];
                } else {
                    parent_nodeinfo_block = child_inode->meta;
                    parent_inode_block = current_inode->links[index_k];
                }
                break;
            } else if (index_j == (num_path_elements - 1) || current_inode->next == 0) {
                // O diretório não foi encontrado
                errno = ENOENT;
                return -1;
            }

            // Pular para o próximo inode
            lseek(sb->fd, current_inode->next * sb->blksz, SEEK_SET);
            read(sb->fd, current_inode, sb->blksz);
        }

        // Salvar informações sobre o diretório anterior
        copy_inode(parent_inode, current_inode, current_nodeinfo);
        copy_nodeinfo(parent_nodeinfo, current_nodeinfo);

        // Pular para o próximo diretório
        copy_inode(current_inode, child_inode, child_nodeinfo);
        copy_nodeinfo(current_nodeinfo, child_nodeinfo);
    }

    // Liberar blocos do nodeinfo e inode do diretório
    fs_put_block(sb, blocks[0]);
    fs_put_block(sb, blocks[1]);

    // Remover o diretório deletado dos links do diretório pai
    for (index_i = 0; index_i < parent_nodeinfo->size; index_i++) {
        if (parent_inode->links[index_i] == blocks[1]) {
            while (index_i < parent_nodeinfo->size - 1) {
                parent_inode->links[index_i] = parent_inode->links[index_i + 1];
                index_i++;
            }
            break;
        }
    }

    // Atualizar informações do diretório pai
    lseek(sb->fd, parent_inode_block * sb->blksz, SEEK_SET);
    write(sb->fd, parent_inode, sb->blksz);

    parent_nodeinfo->size--;
    lseek(sb->fd, parent_nodeinfo_block * sb->blksz, SEEK_SET);
    write(sb->fd, parent_nodeinfo, sb->blksz);

    // Escrever superblock atualizado
    lseek(sb->fd, 0, SEEK_SET);
    write(sb->fd, sb, sb->blksz);

    free(current_inode);
    free(child_inode);
    free(parent_inode);
    free(current_nodeinfo);
    free(child_nodeinfo);
    free(parent_nodeinfo);

    return 0;
}


int fs_mkdir(struct superblock *sb, const char *directory_name) {
    int index, subfolder_index, inode_index, found;
    int path_elements_count;
    uint64_t block_numbers[MAX_FILE_SIZE];
    uint64_t info_block_number, inode_block_number;
    char subfolders[MAX_SUBFOLDERS][MAX_NAME];
    char *token;
    char *path;
    struct inode *current_inode, *child_inode;
    struct nodeinfo *current_node_info, *child_node_info;

    current_inode = (struct inode*) malloc(sb->blksz);
    child_inode = (struct inode*) malloc(sb->blksz);
    current_node_info = (struct nodeinfo*) malloc(sb->blksz);
    child_node_info = (struct nodeinfo*) malloc(sb->blksz);

    path = (char*) malloc(MAX_PATH_NAME*sizeof(char));
    strcpy(path, directory_name);

    // Separar as subpastas em um vetor de strings
    index = 0;
    token = strtok(path, "/"); // Diretório raiz
    while(token != NULL){
        strcpy(subfolders[index], token);
        token = strtok(NULL, "/");
        index++;
    }
    path_elements_count = index;

    // Informações do nodeinfo do diretório raiz
    lseek(sb->fd, sb->blksz, SEEK_SET);
    read(sb->fd, current_node_info, sb->blksz);
    
    // iNode do diretório raiz
    lseek(sb->fd, sb->root*sb->blksz, SEEK_SET);
    read(sb->fd, current_inode, sb->blksz);

    block_numbers[0] = 1;
    block_numbers[1] = 2;

    // Percorrer cada subpasta no caminho, até encontrar o diretório, se existir
    for(subfolder_index = 0; subfolder_index < path_elements_count-1; subfolder_index++){
        // Verificar cada elemento dentro do diretório atual
        while(1){
            found = 0;
            // Verificar se o elemento está no iNode atual
            for(inode_index = 0; inode_index < current_node_info->size; inode_index++){
                // iNode de um arquivo
                lseek(sb->fd, current_inode->links[inode_index]*sb->blksz, SEEK_SET);
                read(sb->fd, child_inode, sb->blksz);
            
                // Verificar se estamos em um iNode filho
                if(child_inode->mode == IMCHILD){
                    // Saltar para o primeiro iNode
                    lseek(sb->fd, child_inode->parent*sb->blksz, SEEK_SET);
                    read(sb->fd, child_inode, sb->blksz);
                }
                
                // Obter as informações do nodeinfo do arquivo
                lseek(sb->fd, child_inode->meta*sb->blksz, SEEK_SET);
                read(sb->fd, child_node_info, sb->blksz);

                if(strcmp(child_node_info->name, subfolders[subfolder_index]) == 0){
                    found = 1;
                    break;
                }
            }

            if(found){
                // A subpasta ou arquivo foi encontrada no iNode atual
                if(subfolder_index == (path_elements_count-2)){
                    block_numbers[0] = child_inode->meta;
                    block_numbers[1] = current_inode->links[inode_index];
                }
                break;
            }else if(subfolder_index == (path_elements_count-2) || current_inode->next == 0){
                // O diretório não foi encontrado
                errno = ENOENT;
                return -1;
            }

            // Saltar para o próximo iNode
            lseek(sb->fd, current_inode->next*sb->blksz, SEEK_SET);
            read(sb->fd, current_inode, sb->blksz);
        }

        // Saltar para o próximo diretório
        copy_inode(current_inode, child_inode, child_node_info);
        copy_nodeinfo(current_node_info, child_node_info);
    }

    // Novo nodeinfo
    info_block_number = fs_get_block(sb);
    child_node_info->size = 0; // O diretório começa vazio
    strcpy(child_node_info->name, subfolders[path_elements_count-1]);

    // Escrever o nodeinfo
    lseek(sb->fd, info_block_number*sb->blksz, SEEK_SET);
    write(sb->fd, child_node_info, sb->blksz);
    
    // Novo iNode
    inode_block_number = fs_get_block(sb);
    child_inode->mode = IMDIR;
    child_inode->parent = inode_block_number;
    child_inode->meta = info_block_number;
    child_inode->next = 0;

    // Escrever o iNode
    lseek(sb->fd, inode_block_number*sb->blksz, SEEK_SET);
    write(sb->fd, child_inode, sb->blksz);

    // Atualizar as informações do diretório pai
    current_inode->links[current_node_info->size] = inode_block_number;
    current_node_info->size++;

    // Escrever as informações atualizadas do nodeinfo do diretório pai
    lseek(sb->fd, block_numbers[0]*sb->blksz, SEEK_SET);
    write(sb->fd, current_node_info, sb->blksz);    

    // Escrever o primeiro iNode atualizado do diretório pai
    lseek(sb->fd, block_numbers[1]*sb->blksz, SEEK_SET);
    write(sb->fd, current_inode, sb->blksz);    

    // Escrever o superblock atualizado
    lseek(sb->fd, 0, SEEK_SET);
    write(sb->fd, sb, sb->blksz);    

    free(current_inode);
    free(child_inode);
    free(current_node_info);
    free(child_node_info);

    return 0;
}

int fs_rmdir(struct superblock *sb, const char *directory_name) {
    int index, subfolder_index, inode_index, found;
    int path_elements_count;
    uint64_t block_numbers[MAX_FILE_SIZE], parent_info_block_number, parent_inode_block_number;
    char subfolders[MAX_SUBFOLDERS][MAX_NAME];
    char *token;
    char *path;

    struct inode *current_inode, *child_inode, *parent_inode;
    struct nodeinfo *current_node_info, *child_node_info, *parent_node_info;

    current_inode = (struct inode*) malloc(sb->blksz);
    child_inode = (struct inode*) malloc(sb->blksz);
    parent_inode = (struct inode*) malloc(sb->blksz);
    current_node_info = (struct nodeinfo*) malloc(sb->blksz);
    child_node_info = (struct nodeinfo*) malloc(sb->blksz);
    parent_node_info = (struct nodeinfo*) malloc(sb->blksz);

    path = (char*) malloc(MAX_PATH_NAME*sizeof(char));
    strcpy(path, directory_name);

    // Separar as subpastas em um vetor de strings
    index = 0;
    token = strtok(path, "/"); // Diretório raiz
    while(token != NULL){
        strcpy(subfolders[index], token);
        token = strtok(NULL, "/");
        index++;
    }
    path_elements_count = index;

    // iNode do diretório raiz
    lseek(sb->fd, sb->root*sb->blksz, SEEK_SET);
    read(sb->fd, current_inode, sb->blksz);

    // Nodeinfo do diretório raiz
    lseek(sb->fd, sb->blksz, SEEK_SET);
    read(sb->fd, current_node_info, sb->blksz);

    block_numbers[0] = 1;
    block_numbers[1] = 2;

    parent_info_block_number = 1; // Nodeinfo do diretório raiz
    parent_inode_block_number = 2; // iNode do diretório raiz

    // Percorrer cada subpasta no caminho
    for(subfolder_index = 0; subfolder_index < path_elements_count; subfolder_index++){
        // Verificar cada elemento dentro do diretório atual
        while(1){
            found = 0;
            // Verificar se o elemento está no iNode atual
            for(inode_index = 0; inode_index < current_node_info->size; inode_index++){
                // iNode de um arquivo
                lseek(sb->fd, current_inode->links[inode_index]*sb->blksz, SEEK_SET);
                read(sb->fd, child_inode, sb->blksz);
            
                // Verificar se estamos em um iNode filho
                if(child_inode->mode == IMCHILD){
                    // Saltar para o primeiro iNode
                    lseek(sb->fd, child_inode->parent*sb->blksz, SEEK_SET);
                    read(sb->fd, child_inode, sb->blksz);
                }
                
                // Obter as informações do nodeinfo do arquivo
                lseek(sb->fd, child_inode->meta*sb->blksz, SEEK_SET);
                read(sb->fd, child_node_info, sb->blksz);

                if(strcmp(child_node_info->name, subfolders[subfolder_index]) == 0){
                    found = 1;
                    break;
                }
            }

            if(found){
                // A subpasta ou arquivo foi encontrada no iNode atual
                if(subfolder_index == (path_elements_count-1)){
                    block_numbers[0] = child_inode->meta;
                    block_numbers[1] = current_inode->links[inode_index];
                }else{
                    parent_info_block_number = child_inode->meta;
                    parent_inode_block_number = current_inode->links[inode_index];
                }
                break;
            }else if(subfolder_index == (path_elements_count-1) || current_inode->next == 0){
                // O diretório não foi encontrado
                errno = ENOENT;
                return -1;
            }

            // Saltar para o próximo iNode
            lseek(sb->fd, current_inode->next*sb->blksz, SEEK_SET);
            read(sb->fd, current_inode, sb->blksz);
        }

        // Salvar informações sobre o diretório anterior
        copy_inode(parent_inode, current_inode, current_node_info);
        copy_nodeinfo(parent_node_info, current_node_info);

        // Saltar para o próximo diretório
        copy_inode(current_inode, child_inode, child_node_info);
        copy_nodeinfo(current_node_info, child_node_info);
    }

    // Verificar se o diretório está vazio
    if(current_node_info->size > 0){
        errno = ENOTEMPTY;
        return -1;
    }

    // Liberar blocos do nodeinfo e iNode do diretório
    fs_put_block(sb, block_numbers[0]);
    fs_put_block(sb, block_numbers[1]);

    // Remover o diretório excluído dos links do diretório pai
    for(index = 0; index < parent_node_info->size; index++){
        if(parent_inode->links[index] == block_numbers[1]){
            while(index < parent_node_info->size-1){
                parent_inode->links[index] = parent_inode->links[index+1];
                index++;
            }
            break;
        }
    }

    // Atualizar as informações do diretório pai
    lseek(sb->fd, parent_inode_block_number*sb->blksz, SEEK_SET);
    write(sb->fd, parent_inode, sb->blksz);

    parent_node_info->size--;
    lseek(sb->fd, parent_info_block_number*sb->blksz, SEEK_SET);
    write(sb->fd, parent_node_info, sb->blksz);

    // Escrever o superblock atualizado
    lseek(sb->fd, 0, SEEK_SET);
    write(sb->fd, sb, sb->blksz);

    free(current_inode);
    free(child_inode);
    free(parent_inode);
    free(current_node_info);
    free(child_node_info);
    free(parent_node_info);

    return 0;
}

char* fs_list_dir(struct superblock* sb, const char* directory_name) {
    int index, subfolder_index, inode_index, position, found;
    int path_elements_count;
    char subfolders[MAX_SUBFOLDERS][MAX_NAME];
    char* token;
    char* path;
    char* elements;
    
    struct inode* current_inode, * child_inode;
    struct nodeinfo* current_node_info, * child_node_info;

    current_inode = (struct inode*) malloc(sb->blksz);
    child_inode = (struct inode*) malloc(sb->blksz);
    current_node_info = (struct nodeinfo*) malloc(sb->blksz);
    child_node_info = (struct nodeinfo*) malloc(sb->blksz);

    path = (char*) malloc(MAX_PATH_NAME * sizeof(char));
    strcpy(path, directory_name);

    // Separar as subpastas em um vetor de strings
    index = 0;
    token = strtok(path, "/"); // Diretório raiz
    while (token != NULL) {
        strcpy(subfolders[index], token);
        token = strtok(NULL, "/");
        index++;
    }
    path_elements_count = index;

    // iNode do diretório raiz
    lseek(sb->fd, sb->root * sb->blksz, SEEK_SET);
    read(sb->fd, current_inode, sb->blksz);

    // Nodeinfo do diretório raiz
    lseek(sb->fd, sb->blksz, SEEK_SET);
    read(sb->fd, current_node_info, sb->blksz);

    // Percorrer cada subpasta no caminho
    for (subfolder_index = 0; subfolder_index < path_elements_count; subfolder_index++) {
        // Verificar cada elemento dentro do diretório atual
        while (1) {
            found = 0;
            // Verificar se o elemento está no iNode atual
            for (inode_index = 0; inode_index < current_node_info->size; inode_index++) {
                // iNode de um arquivo
                lseek(sb->fd, current_inode->links[inode_index] * sb->blksz, SEEK_SET);
                read(sb->fd, child_inode, sb->blksz);
            
                // Verificar se estamos em um iNode filho
                if (child_inode->mode == IMCHILD) {
                    // Saltar para o primeiro iNode
                    lseek(sb->fd, child_inode->parent * sb->blksz, SEEK_SET);
                    read(sb->fd, child_inode, sb->blksz);
                }
                
                // Obter as informações do nodeinfo do arquivo
                lseek(sb->fd, child_inode->meta * sb->blksz, SEEK_SET);
                read(sb->fd, child_node_info, sb->blksz);

                if (strcmp(child_node_info->name, subfolders[subfolder_index]) == 0) {
                    found = 1;
                    break;
                }
            }

            if (found) {
                // A subpasta ou arquivo foi encontrada no iNode atual
                break;
            } else if (subfolder_index == (path_elements_count - 1) || current_inode->next == 0) {
                // O diretório não foi encontrado
                errno = ENOENT;
                elements = (char*) malloc(3 * sizeof(char));
                strcpy(elements, "-1");
                return elements;
            }

            // Saltar para o próximo iNode
            lseek(sb->fd, current_inode->next * sb->blksz, SEEK_SET);
            read(sb->fd, current_inode, sb->blksz);
        }

        // Saltar para o próximo diretório
        copy_inode(current_inode, child_inode, child_node_info);
        copy_nodeinfo(current_node_info, child_node_info);
    }

    elements = (char*) malloc(MAX_PATH_NAME * sizeof(char));
    position = 0;
    elements[0] = '\0';

    for (index = 0; index < current_node_info->size; index++) {
        // iNode de um arquivo
        lseek(sb->fd, current_inode->links[index] * sb->blksz, SEEK_SET);
        read(sb->fd, child_inode, sb->blksz);
    
        // Verificar se estamos em um iNode filho
        if (child_inode->mode == IMCHILD) {
            // Saltar para o primeiro iNode
            lseek(sb->fd, child_inode->parent * sb->blksz, SEEK_SET);
            read(sb->fd, child_inode, sb->blksz);
        }
        
        // Obter as informações do nodeinfo do arquivo
        lseek(sb->fd, child_inode->meta * sb->blksz, SEEK_SET);
        read(sb->fd, child_node_info, sb->blksz);

        strcpy((elements + position), child_node_info->name);
        position += strlen(child_node_info->name);

        if (child_inode->mode == IMDIR) {
            // É um diretório
            elements[position] = '/';
            elements[position + 1] = '\0';
            position++;
        }

        if (index < current_node_info->size - 1) {
            elements[position] = ' ';
            elements[position + 1] = '\0';
            position++;
        }
    }

    free(current_inode);
    free(child_inode);
    free(current_node_info);
    free(child_node_info);

    return elements;
}
