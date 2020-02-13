#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
//规定：
//(1)数据文件命名是有扩展名的，目录文件是没有的，目录文件中的exname是空"" 
//(2)目录文件中，第一个FCB是放根目录的，第二个才是放当前目录？？ 
//(3)'.'表示当前目录，".."表示父目录
//(4)子目录如果cd打开，那么父目录也在用户打开文件表
//(5)useropen中的dir(记录路径的)是不包括文件名本身的（针对数据文件）,目录文件的话是包含本身的
//(6)my_open、my_close、my_read、my_write、my_rm只能是对数据文件，不能对目录文件，但是do_write、do_read是针对所有的文件的
//(9)my_cd()中，不能有这样的结构"../user"
//(10)do_close、do_open是不检查fd的，需要在调用他的地方检查？？ 
#define MAXOPENFILE 10//最大的打开文件数（就是打开文件表的大小。包括目录文件-由于打开一个文件时其父目录的FCB也被打开，也就是说文件系统最多只能由十级）
#define SIZE 1024000//虚拟磁盘空间大小1000个块 一块1024KB 
#define BLOCKSIZE 1024
#define END 65535//FAT表的结束符FFFF 
#define FREE 0//FAT表的空符 
#define BLOCKNUM 1000
#define MAXTEXTSIZE 2048//用户一次最多输入多少字符,比缓冲区要大


typedef struct USEROPEN
{
	
	//fcb中的信息
	char filename[8];
	char exname[3];
	unsigned char attribute;//0表示目录文件，1表示数据文件
	unsigned short time;
	unsigned short date;
	unsigned short first;//文件起始盘块号
	unsigned long length;

	//动态信息
	/*dir：打开文件的路径，以快速检查文件是否打开
		注：目录文件的话包括该目录的名，数据文件到其所在的目录为止
	*/
	char dir[80];
	int topenfile;//表示这个表项是否被占用
	int father;//对应的目录
	int dirno;//存放的是父目录的第一个磁盘号
	int diroff;//文件的fcb在父目录文件中的相对偏移量- dirno+diroff得到文件fcb所在位置 
	int count;//读写指针在文件中的位置
	char fcbstate;//表示文件的FCB是否被修改 


}useropen;


typedef struct BLOCK0
{
	char information[200];
	unsigned short root;//根目录文件的起始盘块号
	unsigned char *startblock;//虚拟磁盘上数据区的开始位置？？ 
}block0;

typedef struct FAT
{
	unsigned short id;
}fat;

typedef struct FCB
{
	char filename[8];
	char exname[3];
	unsigned char attribute;//0表示目录文件，1表示数据文件
	unsigned short time;
	unsigned short date;
	unsigned short first;
	unsigned long length;
	char free;//0表示空，1表示占用
}fcb;

/*format排磁盘，starsys初始打开用户表*/ 
/*全局变量*/
unsigned char* myvhard;//main里申请出空间和地址，format设为虚拟磁盘的总起始位置（引导块的位置）char类型是因为要一个一个字节读
useropen openfilelist[MAXOPENFILE];//format建立，startsys初始化 
char currentdir[80];//startsys获得。currentdir/curdir:记录当前目录的目录名（包括路径）。注：如果现在指向的数据文件，那么是她所在的目录
int curdir;//startsys获得。当前目录文件描述符fd:就是该文件在用户打开文件表中的序号 
unsigned char *startp;//startsys获得。记录虚拟磁盘数据区开始的位置？？ 为什么要记录两次 
char myfilesys[]="myfilesys";

void startsys()
{
	FILE *filesys;
	int i;
	fcb *t_ptr;


	filesys=fopen(myfilesys,"r");
	if(filesys)
	{
		fread(myvhard,SIZE,1,filesys);
		fclose(filesys);

	}
	else
	{
		printf("The filesystem is not build yet....\n");
		fclose(filesys);
		my_format();
	}

	for(i=0;i<MAXOPENFILE;i++)
    {
        openfilelist[i].topenfile=0;
    }
	t_ptr=(fcb *)(myvhard+5*BLOCKSIZE);//第一个fcb指向数据区开头 
	strcpy(openfilelist[0].filename,t_ptr->filename);
	strcpy(openfilelist[0].exname,t_ptr->exname);
	openfilelist[0].attribute=t_ptr->attribute;
	openfilelist[0].time=t_ptr->time;
	openfilelist[0].date=t_ptr->date;
	openfilelist[0].first=t_ptr->first;
	openfilelist[0].length=t_ptr->length;

	openfilelist[0].count=0;
	openfilelist[0].fcbstate=0;
	openfilelist[0].topenfile=1;
	openfilelist[0].father=0;//根目录文件的父目录就是自己 （目录文件的父目录是自己吗？） 
	openfilelist[0].dirno=5;
	openfilelist[0].diroff=0;

	curdir=0;
	memset(currentdir,'\0',sizeof(currentdir));
	strcpy(currentdir,"root");
	strcpy(openfilelist[0].dir,currentdir);
	startp=(unsigned char*)(myvhard+5*BLOCKSIZE);
}



/*初始化虚拟磁盘空间，并且建立根目录文件、创建根目录的ＦＣＢ
会保存到真实的文件中,!!!!!内存空间已经有了!!!!!*/
void my_format()
{
	unsigned char* t_ptr;
	block0 *guide;
	fat *fat1,*fatbak;
	fcb *root;
	time_t *now;
	struct tm *nowtime;
	FILE *filesys;
	int i;
/*（1）初始化引导块（分配位置+内容）*/ 
	t_ptr=myvhard;
	guide=(block0 *)t_ptr;
	strcpy(guide->information,"The file_system's Block_Size=1024B\tBlock_Num=100\nFAT_1 begin at No.1\tFAT_1_Num=2\nFAT_bak begin at No.3 FAT_bak_Num=2\nRootBlock begin at No.5\tRootBlockNum=2\n ");
	guide->root=5;
	guide->startblock=(unsigned char *)(myvhard+5*BLOCKSIZE);
/*（2）初始化fat表（分配位置+内容）*/ 
	fat1=(fat *)(myvhard+1*BLOCKSIZE);
	fatbak=(fat *)(myvhard+3*BLOCKSIZE);
	fat1->id=END;fatbak->id=END;//0号块
	fat1++;fatbak++;
	fat1->id=2;fatbak->id=2;//1号块
	fat1++;fatbak++;
	fat1->id=END;fatbak->id=END;//2号块
	fat1++;fatbak++;
	fat1->id=4;fatbak->id=4;//3号块
	fat1++;fatbak++;
	fat1->id=END;fatbak->id=END;//4号块
	fat1++;fatbak++;
	fat1->id=6;fatbak->id=6;//5号块
	fat1++;fatbak++;
	fat1->id=END;fatbak->id=END;//6号块
	fat1++;fatbak++;//上图end;2;end;4;end;6;end;即0号块，1号和2号块，3号和4号块，5号和6号块 
	for(i=7;i<BLOCKNUM;i++)//其余块全置为空 
	{
		fat1->id=FREE;
		fatbak->id=FREE;
		fatbak++;fat1++;
	}
/*（3）初始化根目录数据区，数据区的第一个fcb是根目录文件"."的fcb(fcb是root)，文件名"."文件内容已填写长度为2个fcb（根目录和当前目录的fcb)，总容量为2块（5号和6号块）*/ 
	t_ptr=myvhard+BLOCKSIZE*5;
	root=(fcb *)(myvhard+BLOCKSIZE*5);
	strcpy(root->filename,".");
	strcpy(root->exname," ");
	root->attribute=0;
	root->first=5;
	root->length=2*sizeof(fcb);
/*（4）获得根目录文件的创建时间等*/ 
	now=(time_t *)malloc(sizeof(time_t));
	time(now);
	nowtime=localtime(now);
	root->time=nowtime->tm_hour*2048+nowtime->tm_min*32+nowtime->tm_sec/2;//time是16位二进制数，其中小时16-12共5位，分11-6共6位，因为剩下5-1共5位不够放秒所以要除以2 
	root->date=(nowtime->tm_year-80)*512+(nowtime->tm_mon+1)*32+nowtime->tm_mday;//data是16位二进制数，其中年16-10共7位，月9-6共4位
/*（5）初始化数据区的第二个fcb是当前目录文件".."的fcb，文件名".."  因为此时根目录就是当前目录，所以其它部分同根目录一致，如文件内容已填写长度为2个fcb（根目录和当前目录的fcb)，总容量为2块（5号和6号块） */  
	root++;
	strcpy(root->filename,"..");
	strcpy(root->exname," ");
	root->attribute=0;
	root->first=5;
	root->length=2*sizeof(fcb);

	now=(time_t *)malloc(sizeof(time_t));
	time(now);
	nowtime=localtime(now);
	root->time=nowtime->tm_hour*2048+nowtime->tm_min*32+nowtime->tm_sec/2;//time是16位二进制数，其中小时16-12共5位，分11-6共6位，因为剩下5-1共5位不够放秒所以要除以2 
	root->date=(nowtime->tm_year-80)*512+(nowtime->tm_mon+1)*32+nowtime->tm_mday;//data是16位二进制数，其中年16-10共7位，月9-6共4位
/*（6）把修改后的文件系统整个写进该文件里*/ 
	filesys=fopen(myfilesys,"w");
	fwrite(myvhard,SIZE,1,filesys);
	fclose(filesys);

	free(now);
}


/*读出一定长度的文件内容到用户空间中(包括数据文件,不包括目录文件)*/
int my_read(int fd,int len)
{
	int i;
	int readsize;
	char text[MAXTEXTSIZE];

	if((fd<0)||(fd>=MAXOPENFILE))
	{
		printf("Error:the file is not exist...\n");
		return -1;
	}
	if((openfilelist[fd].topenfile==0)||(openfilelist[fd].attribute==0))
	{
		printf("Error:the file is not exist...\n");
		return -1;
	}

	openfilelist[fd].count=0;//文件头指针读起 
	readsize=do_read(fd,len,text);
	if(readsize==-1)
	{
		printf("Error:readfile err....\n");
	}
	else
	{
		printf("%s\n",text );
	}

	return readsize;

}



/*从磁盘读到内存的指定位置中，返回读入的字节数*/
/*
step1：文件读写指针的值所在的物理磁盘号
step2：将这个磁盘读到缓冲区中，在将读写指针位置开始到所要求长度的部分读到text的用户要求的空间中去，
如果长度比较长，在下一个物理磁盘上，那么接着读，直到完成
step3：返回值是读到的字节数
*/
int do_read(int fd,int len,char *text)
{
	unsigned char *buf;
	unsigned short blkno,blkoff;
	fat *fat1,*fatptr;
	int readsize;
	unsigned char *readptr;
	int flag=0;
	int i;

	buf=(unsigned char *)malloc(BLOCKSIZE);//给缓冲区分配一块的大小区域 
	if(buf==NULL)
	{
		printf("Error:the buf apply is failed...\n");
		return -1;
	}
/*（1）找到读的所在块号，以及读的起始位置在快内的偏移量*/ 
	blkno=openfilelist[fd].first;
	blkoff=openfilelist[fd].count;
	if(blkoff>=openfilelist[fd].length)
	{
		printf("Error:Reading file is out of range...\n");
		free(buf);
		return -1;
	}
	fat1=(fat *)(myvhard+BLOCKSIZE);//查fat1表 
	fatptr=fat1+blkno;
	while(blkoff>=BLOCKSIZE)//通过查fat1表挪动块号，找到count所在块号 赋值给blkno。此时blkno从文件内偏移量转变为块内偏移量 
	{
		blkno=fatptr->id;
		fatptr=fat1+blkno;
		blkoff=blkoff-BLOCKSIZE;
	}
/*（2）通过读的所在块号，算出物理地址，并将整块内容读入缓冲区，再从读的起始位置在块内的偏移量将内容从缓冲区读入text字符串（用户空间）*/ 
	readsize=0;
	while(readsize<len)
	{
		readptr=(unsigned char *)(myvhard+BLOCKSIZE*blkno);//找到了读的起始块地址readptr 
		for(i=0;i<BLOCKSIZE;i++)//放进缓冲区 
		{
			buf[i]=readptr[i];
		}
		for(;blkoff<BLOCKSIZE;blkoff++)
		{
			text[readsize++]=buf[blkoff];
			openfilelist[fd].count++;
			if((readsize==len)||(openfilelist[fd].length==openfilelist[fd].count))//在此块内读到指定长度或读写指针已经走到文件末尾 
			{
				flag=1;
				break;
			}
		}
		if(flag==1)
		{
			break;
		}
		else
		{
			blkno=fatptr->id;
			if(blkno==END)
			{
				break;
			}
			blkoff=0;
			fatptr=fat1+blkno;
		}
	}
	text[readsize]='\0';
	free(buf);
	return readsize;

}



//在do_write()中改的openfilelist的：count，fcbstate,length
/*根据不同的写方式，将用户输入的内容改写到文件中，将其保存到磁盘上(只能是数据文件)
1.会设置count的位置
2.没有改变目录文件中所写文件的FCB中的length？？为什么不改变*/
int my_write(int fd)
{
	int wstyle;
	unsigned short blkno,blkoff;
	fat *fat1,*fatptr,*fat_bak,*fat_bakptr;
	char text[MAXTEXTSIZE];
	int writesize;
	int t_size,len;

	if(fd<0||fd>=MAXOPENFILE||openfilelist[fd].topenfile==0||openfilelist[fd].attribute==0)//最后一个是判断是不是目录文件
	{
		printf("Error:the write file is not exist...\n");
		return -1;
	}
/*(1)选择写方式*/
	while(1)
	{
		printf("chose the write styles:\n1.cut write\t2.cover write\t3.append write\n");//cut重写，cover从当前指针写 
		scanf("%d",&wstyle);
		if((wstyle<4)&&(wstyle>0))
		{
			break;
		}
		printf("input Error...\n");
	}
	getchar();//将换行符拿掉
	fat1=(fat *)(myvhard+BLOCKSIZE);
	fat_bak=(fat *)(myvhard+3*BLOCKSIZE);
	switch(wstyle)
	{
		case 1://截断写- 重头写 
		{/*（2）重置fat表（第一块置为END其余置为FREE*/ 
			blkno=openfilelist[fd].first;
			fatptr=fat1+blkno;
			fat_bakptr=fat_bak+blkno;
			blkno=fatptr->id;
			fatptr->id=END;
			fat_bakptr->id=END;//保留第一块磁盘
			while(blkno!=END)
			{
				fatptr=fat1+blkno;
				fat_bakptr=fat_bak+blkno;
				blkno=fatptr->id;
				fatptr->id=FREE;
				fat_bakptr->id=FREE;
			}
			/*(3)找到读写指针位置，修改用户打开文件表中存的初始文件长度*/
			openfilelist[fd].length=0;
			openfilelist[fd].count=0;
			break;
		}
		case 2://覆盖写-从当前指针写 
		{
			//$openfilelist[fd].count=0;
			break;
		}
		case 3://追加写
		{
			openfilelist[fd].count=openfilelist[fd].length;
			break;
		}
	}

	writesize=0;
    printf("please input write data(end with Ctrl+Z):\n");
	while(gets(text))//text内不含换行符 
	{
		len=strlen(text);
		text[len++]='\0';
		len--;//进入的text大小不包括\0也就是说不写入\0 ，直到全部写完再写 
		t_size=do_write(fd,text,len);
		if(t_size!=-1)
		{
			writesize+=t_size;
		}
		len++; 
		if(t_size<len)
		{
			printf("Error:write file err...\n");
			break;
		}
	}
	return writesize;
}



/*将用户空间中的内容通过缓冲区输出到磁盘中，返回实际写的数据*/
/*注意：
1.写的时候fat表和fat_bak表都要变化
2.只更改了openfilelist中的该项的信息，目录文件是没有改变的
*/
int do_write(int fd,char *text,int len)
{
	unsigned char *buf;
	fat *fat1,*fatptr,*fat_bak,*fat_bakptr;
	unsigned short blkoff,blkno;
	int writesize;
	unsigned char *writeptr;
	int i;

	buf=(unsigned char *)malloc(BLOCKSIZE);
	if(buf==NULL)
	{
		printf("Error:the buf apply is failed...\n");
		return -1;
	}

	fat1=(fat *)(myvhard+BLOCKSIZE);
	fat_bak=(fat *)(myvhard+BLOCKSIZE*3);
	blkno=openfilelist[fd].first;
	blkoff=openfilelist[fd].count;
	fatptr=fat1+blkno;
	fat_bakptr=fat_bak+blkno;
	/*（1）找到读写指针的所在块号，以及写的起始位置在快内的偏移量（0-1023）*/ 
	while(blkoff>=BLOCKSIZE)//(1）找到读写指针count所在盘块给blkno 和快内偏移量 blkoff 
	{
		blkno=fatptr->id;
		blkoff=blkoff-BLOCKSIZE;
		if(blkno!=END)
		{
			fatptr=fat1+blkno;
			fat_bakptr=fat_bak+blkno;

		}
		else//当读写指针刚好在块末尾，且要追加写时。由于（追加写时，count=lenth，但count从0计数，而lenth从1计数）count的位置已经超出了文章末尾1个位置
		{//则添加一个新块 
			blkno=findblock();
			if(blkno==-1)
			{
				free(buf);
				return -1;
			}
			fatptr->id=blkno;
			fat_bakptr->id=blkno;
			fatptr=fat1+blkno;
			fat_bakptr=fat_bak+blkno;
			fatptr->id = END;
            fat_bakptr->id = END;
		}
	}
	/*（2）通过写指针的所在块号，算出写的物理地址，并将整块内容读入缓冲区*/
	writesize=0;
	while(writesize<len) 
	{
		writeptr=(unsigned char *)(myvhard+blkno*BLOCKSIZE);
		for(i=0;i<BLOCKSIZE;i++)
		{
			//先将磁盘中的count第一块的数据读出来
			buf[i]=writeptr[i];
		}
	/*（3）从写的起始位置在块内的偏移量将内容text先写入缓冲区*/ 
		for(;blkoff<BLOCKSIZE;blkoff++)
		{
			buf[blkoff]=text[writesize++];//若blkoff= 
			openfilelist[fd].count++;
			if(writesize==len)
			{
				break;//要么写完缓冲区退出要么写够长度退出 
			}
		}
	/*（4）将缓冲区的内容存放到磁盘中去（物理空间）*/
		for(i=0;i<BLOCKSIZE;i++)
		{
			writeptr[i]=buf[i];
		}
	/*（5）若已写部分没达到要求长度，则再增加磁盘块写*/ 
		if(writesize<len)
		{
			blkno=fatptr->id;
			if(blkno==END)
			{
				blkno=findblock();
				if(blkno==-1)
				{
					break;
				}
				fatptr->id=blkno;
				fat_bakptr->id=blkno;
				fatptr=fat1+blkno;
				fat_bakptr=fat_bak+blkno;
				fatptr->id=END;
				fat_bakptr->id=END;
			}
			else
			{
				fatptr=fat1+blkno;
				fat_bakptr=fat_bak+blkno;
			}
			blkoff=0;
		}
	}
	/*（6）修改用户打开文件表里的文件长度*/ 
	if(openfilelist[fd].count>openfilelist[fd].length)
	{
		openfilelist[fd].length=openfilelist[fd].count;//注意这里，因为可以覆盖
	}
	/*(7)给文件结尾加上结束符\0 */
	if(writeptr[length-1]!='\0')
	{
		writeptr[length]='\0';
		openfilelist[fd].count++;
		openfilelist[fd].length++;
	} 

	openfilelist[fd].fcbstate=1;//表示修改过
	free(buf);
	return writesize;
}


	/*显示当前目录的内容（包括子目录和文件信息）*/
void my_ls()
{
	char text[MAXTEXTSIZE];
	fcb *fcbptr;
	int i;
	int readsize;
//(1)将当前目录文件内容全部读入text 
	openfilelist[curdir].count=0;
	readsize=do_read(curdir,openfilelist[curdir].length,text);
//（2）将text内容整理按条输出 
	fcbptr=(fcb *)text;
	for(i=0;i<readsize/sizeof(fcb);i++)
	{
		if(fcbptr->free==1)
		{
			if(fcbptr->attribute==0)//目录文件
			{
                printf("%s\\\t\t<DIR>\t\t%d/%d/%d\t%02d:%02d:%02d\n", fcbptr->filename, (fcbptr->date >> 9) + 1980, (fcbptr->date >> 5) & 0x000f, fcbptr->date & 0x001f, fcbptr->time >> 11, (fcbptr->time >> 5) & 0x003f, fcbptr->time & 0x001f * 2);
			}
			else//数据文件
			{
                printf("%s.%s\t\t%dB\t\t%d/%d/%d\t%02d:%02d:%02d\t\n", fcbptr->filename, fcbptr->exname, (int)(fcbptr->length), (fcbptr->date >> 9) + 1980, (fcbptr->date >> 5) & 0x000f, fcbptr->date & 0x1f, fcbptr->time >> 11, (fcbptr->time >> 5) & 0x3f, fcbptr->time & 0x1f * 2);
			}
		}
		fcbptr++;
	}
}


/*在当前文件目录下创建文件（不是目录），但是并不打开
1.父目录中的这个文件的fcb已经修改好了，本身这个文件的信息也都已经好了*/
void my_create(char *filename)
{
	char *fname,*exname;
	char text[MAXTEXTSIZE];
	int i;
	int readsize;
	fcb *fcbptr;
	fat *fat2,*fat1;
	unsigned short blkno;
    time_t now;
    struct tm *nowtime;
    /*获得文件名和扩展名，并验证其有效性*/ 
	fname=strtok(filename,".");//strtok用于分隔文件名和扩展名 
	exname=strtok(NULL,".");
    if(fname==NULL)
    {
        printf("Error：creating file must have a right name.\n");
        return ;
    }
    if(exname==NULL)
    {
        printf("Error:creating file must have a extern name.\n");
        return ;
    }
	openfilelist[curdir].count=0;
	/*（2）查重*/ 
	readsize=do_read(curdir,openfilelist[curdir].length,text);
	fcbptr=(fcb *)text;
	for(i=0;i<readsize/sizeof(fcb);i++)
	{
		if((fcbptr->free==1)&&(strcmp(fname,fcbptr->filename)==0)&&(strcmp(exname,fcbptr->exname)==0))
		{
			printf("Error:the filename is already exist...\n");
			return ;
		}
		fcbptr++;
	}
	/*（3）找一个空的fcb*/ 
	fcbptr=(fcb *)text;
	for(i=0;i<readsize/sizeof(fcb);i++)
	{
		if(fcbptr->free==0)
		{
			break;
		}
		fcbptr++;
	}
	/*（4）找一个空块*/ 
	blkno=findblock();
	if(blkno==-1)
	{
		return;
	}
	fat1=(fat *)(myvhard+BLOCKSIZE);
	fat2=(fat *)(myvhard+3*BLOCKSIZE);
	(fat1+blkno)->id=END;
	(fat2+blkno)->id=END;


	/*（5）修改文件fcb内容*/ 
    now = time(NULL);
    nowtime = localtime(&now);
    strcpy(fcbptr->filename,fname);
    strcpy(fcbptr->exname,exname);   
    fcbptr->attribute=1;
	fcbptr->time = nowtime->tm_hour * 2048 + nowtime->tm_min * 32 + nowtime->tm_sec / 2;
    fcbptr->date = (nowtime->tm_year - 80) * 512 + (nowtime->tm_mon + 1) * 32 + nowtime->tm_mday;
    fcbptr->first = blkno;
    fcbptr->length=0;
    fcbptr->free=1;
	/*（6）修改父目录文件里的相关内容*/ 
    openfilelist[curdir].count=i*sizeof(fcb);
    do_write(curdir,(char *)fcbptr,sizeof(fcb));
    /*（6）修改父目录文件fcb的内容*/ 
    fcbptr = (fcb *)text;
    fcbptr->length = openfilelist[curdir].length;
    openfilelist[curdir].count = 0;
    
    do_write(curdir, (char *)fcbptr, sizeof(fcb));//??为什么要往当前目录文件（就是新文件的父目录文件）里写进 新文件的父目录文件自己的pcb 

    openfilelist[curdir].fcbstate = 1;

}


/*找到一块空闲的磁盘block*/
int findblock()
{
	unsigned short i;
	fat *fat1,*fatptr;
	fat1=(fat *)(myvhard+BLOCKSIZE);
	for(i=7;i<BLOCKNUM;i++)
	{
		fatptr=fat1+i;
		if(fatptr->id==FREE)
		{
			return i;
		}
	}
	printf("Error:can't find a free block...\n");
	return  -1;
}



/*返回openfilename（用户打开文件表）中的空闲表项*/
int findopenfile()
{
	int i;
	for(i=0;i<MAXOPENFILE;i++)
	{
		if(openfilelist[i].topenfile==0)
		{
			return i;
		}
	}
	printf("Error:you shuold close some file first...\n");
	return -1;
}



/*删除当前目录下的文件（不包括目录）
1.父目录、本身文件、openfilelist都已经修改好了*/
void my_rm(char *filename)
{
	unsigned short blkno;
	char *fname,*exname;
	int i;
	int readsize;
	char text[MAXTEXTSIZE];
	fat *fat1,*fat2,*fatptr1,*fatptr2,*fatptr;
	fcb *fcbptr;
	/*(1)分离文件名和扩展名，判断有效性*/
	fname=strtok(filename,".");
	exname=strtok(NULL,".");
	if(fname==NULL)
	{
        printf("Error:removing file must have a right name...\n");
        return;
	}
	if(exname==NULL)
	{
        printf("Error:removing file must have a extern name...\n");
        return;
	}
	/*(2)查找文件是否存在*/
	openfilelist[curdir].count=0;
	readsize=do_read(curdir,openfilelist[curdir].length,text);
	fcbptr=(fcb *)text;
	for(i=0;i<readsize/sizeof(fcb);i++)
	{
		if(fcbptr->free==1)
		{
			if((strcmp(fcbptr->filename,fname)==0)&&(strcmp(fcbptr->exname,exname)==0))
			{
				break;
			}
		}
		fcbptr++;
	}
	if(i==readsize/sizeof(fcb))
	{
		printf("Error:the file is not exist...\n");
		return;
	}
	/*(3)清空文件是所在的fat表内容*/
	blkno=fcbptr->first;
	fat1=(fat *)(myvhard+BLOCKSIZE);
	fat2=(fat *)(myvhard+3*BLOCKSIZE);
	while(blkno!=END)
	{
		fatptr1=fat1+blkno;
		fatptr2=fat2+blkno;
		blkno=fatptr1->id;
		fatptr1->id=FREE;
		fatptr2->id=FREE;
	}
	/*(4)修改文件fcb为未使用*/
	fcbptr->free=0;
	/*(5)将修改后的该文件pcb更新到文件父目录内容*/
	openfilelist[curdir].count=i*sizeof(fcb);
	do_write(curdir,(char *)fcbptr,sizeof(fcb));
	openfilelist[curdir].fcbstate=1; //？？更改的是文件的fcb，这里的curdir指向的不是其父目录吗？其父目录的fcb并没有更改 
}



/*打开文件(只能打开数据文件,可以是指定目录的)*/
int my_open(char *filename)
{
	int i;
	char *tfname,*exname,*path,*fname,*str;
	char tfile[11];
	int fd;
	char recover_dir[80],texname[3],tpfname[8];
	
    strcpy(tfile,filename);
    
	tfname=strtok(filename,".");
	exname=strtok(NULL,".");
	if(exname==NULL)//如果是目录
	{
		printf("Error:can't open a dir...\n");
		return -1;
	}
	else//如果是数据文件
	{
		/*（1）直到找到path存放相对路径，fname存放文件名*/
		path=strtok(tfname,"/");
		fname=strtok(NULL,"/");
		str=strtok(NULL,"/");//若是ROOT/A/B/C/TEXT ;path=root;fname=A;str=B??path=A;fname=B;str=C;当前A， path=B;fname=C;str=text
		if(fname)//表示不是当前目录
		{
			strcpy(recover_dir,currentdir);//保存现在目录的完整路径
			while(str)
			{
			    strcpy(tpfname,fname);
				strcat(path,"/");
				strcat(path,tpfname);
				fname=str;
				str=strtok(NULL,"/");
			}
			/*（2）按路径cd进入，按文件名.扩展名打开*/
			my_cd(path);
			strcpy(texname,exname);
			strcat(fname,".");
			strcat(fname,texname);
			fd=do_open(fname);
			if(fd==-1)//说明打开失败了
			{
				my_cd(recover_dir);//恢复原来的目录
				return -1;
			}
			return fd;
		}
		else//表示是当前目录
		{
			do_open(tfile);
			return fd;
		}
	}

}


/*在当前目录下打开文件，包括数据文件和目录文件
0.返回的是打开的文件或者目录所在的openfilelist的index
1.如果是目录文件，openfilelist中的dir还需要改变*/
int do_open(char *filename)
{
	int i;
	char exname[3];
	char *fname,*str;
	int readsize,fd;
	char text[MAXTEXTSIZE];//缓冲区
	fcb *fcbptr;

    fname = strtok(filename, ".");
    str = strtok(NULL, ".");
    if(str)//数据文件
        strcpy(exname, str);
    else//目录文件
    	strcpy(exname, " ");
	/*（1）判断该文件是否已经打开*/ 
	for(i=0;i<MAXOPENFILE;i++)
	{
		if(openfilelist[i].topenfile==1)
		{
			if((strcmp(fname,openfilelist[i].filename)==0)&&(strcmp(exname,openfilelist[i].exname)==0))//文件名和扩展名相同
			{
				printf("Error:the file is already opened....\n");
				return -1;
			}
		}

	}
	/*（2）判断是否已存在，若存在找到其pcb在该目录文件下的位置*/ 
	openfilelist[curdir].count=0;
	readsize=do_read(curdir,openfilelist[curdir].length,text);
	fcbptr=(fcb *)text;
	for(i=0;i<readsize/sizeof(fcb);i++)
	{
		if(fcbptr->free&&(strcmp(fcbptr->filename,fname)==0)&&(strcmp(fcbptr->exname,exname)==0))
		{
			break;
		}
		fcbptr++;
	}
	if(i==readsize/sizeof(fcb))
	{
		printf("Error:the file or dir is not exist...\n");
		return -1;
	}
	/*（3）分配一个用户打开文件的目录项，并给各项赋值*/ 
	fd=findopenfile();
	if(fd==-1)
	{
		return -1;
	}
	strcpy(openfilelist[fd].filename, fcbptr->filename);
    strcpy(openfilelist[fd].exname, fcbptr->exname);
    openfilelist[fd].attribute = fcbptr->attribute;
    openfilelist[fd].time = fcbptr->time;
    openfilelist[fd].date = fcbptr->date;
    openfilelist[fd].first = fcbptr->first;
    openfilelist[fd].length = fcbptr->length;

    openfilelist[fd].dirno = openfilelist[curdir].first;
    openfilelist[fd].diroff = i;
    strcpy(openfilelist[fd].dir, openfilelist[curdir].dir);
    /*（4）判断如果是目录，则修改其用户打开文件表下的dir值（目录文件的dir包括自身）*/ 
    if(openfilelist[fd].attribute==0)//如果是目录
    {
    	strcat(openfilelist[fd].dir,"/");
    	strcat(openfilelist[fd].dir,openfilelist[fd].filename);
    }
    openfilelist[fd].father = curdir;
    openfilelist[fd].count = 0;
    openfilelist[fd].fcbstate = 0;
    openfilelist[fd].topenfile = 1;
    return fd;
}


/*关闭my_open()打开的文件
1.因为my_open只是打开数据文件，所以这里关闭的也是数据文件
2.返回值是父目录的openfilelist中的index
3.这里什么也不做，全部是在do_close中做的*/
int my_close(int fd)
{
	int father;

	if(fd>=MAXOPENFILE||fd<0||(openfilelist[fd].topenfile==0)||(openfilelist[fd].attribute==0))//最后是判断是否为数据文件
	{
		printf("Error:the file is not open....\n");
		return -1;
	}

	father=do_close(fd);
	return father;

}



/*可以关闭目录文件，数据文件
1.返回值是父目录的openfilelist中的index
2.要检查openfilelist中的fcbstate的，然后在其父目录中要做相应的改变*/
int do_close(int fd)
{
	fcb *fcbptr;
	int father;

	/*（1）检查被打开过的文件FCB是否被修改过，如果是，则将修改后的fcb更新保存到父目录中去*/ 
	if(openfilelist[fd].fcbstate)
	{
		fcbptr=(fcb *)malloc(sizeof(fcb));
		strcpy(fcbptr->filename,openfilelist[fd].filename);
		strcpy(fcbptr->exname,openfilelist[fd].exname);
		fcbptr->attribute=openfilelist[fd].attribute;
		fcbptr->time=openfilelist[fd].time;
		fcbptr->date=openfilelist[fd].date;
		fcbptr->first=openfilelist[fd].first;
		fcbptr->length=openfilelist[fd].length;
		fcbptr->free=1;
		
		father=openfilelist[fd].father;		
		openfilelist[father].count=openfilelist[fd].diroff*sizeof(fcb);		 
		do_write(father,(char *)fcbptr,sizeof(fcb));
		free(fcbptr);
		openfilelist[fd].fcbstate=0;
	}
	/*（2）清空openfilelist的表项*/ 
	openfilelist[fd].topenfile=0;
	strcpy(openfilelist[fd].filename,"\0");
	strcpy(openfilelist[fd].exname,"\0");
	return openfilelist[fd].father;
}

//改变currentdir,curdir
/*更改当前目录
1.其父目录打开之后不会关闭,还会把原来的所有目录都关闭),
2.可以打开指定的路径，如果路径错误，返回原来的目录*/
void my_cd(char *dirname)
{
	char *dir;
	int i;
	char recover_dir[80];
	char tdir[80];
	int next_curdir;
	int fd;
	dir=strtok(dirname,"/");
//说明cd操作只有./或../或目录路径名不支持./目录路径或../目录路径 。且不支持相对路径 
	if(strcmp(dir,".")==0)//当前目录
	{
		dir=strtok(NULL,"/");
		if(dir==NULL)
		{
			return;
		}
		else
		{
			printf("Error:the dirname is illegal...\n");
			return;
		}
	}
	else if(strcmp(dir,"..")==0)//父目录
	{

		dir=strtok(NULL,"/");
		if(dir==NULL)
		{
			if(curdir)
			{
				curdir=do_close(curdir);
				memset(currentdir,'\0',sizeof(currentdir));
				strcpy(currentdir,openfilelist[curdir].dir);
				return;
			}
		}
		else
		{
			printf("Error:the dirname is illegal...\n");
			return;
		}

	}
	else if(strcmp(dir,"root")==0)
	{
		/*（1）判断是否已经在用户打开文件表里，（如root/A/B/C,当前在root/A/B/C/D的情况） */
		for(i=0;i<MAXOPENFILE;i++)
		{
			if((openfilelist[i].attribute==0)&&openfilelist[i].topenfile)
			{
				if(strcmp(openfilelist[i].dir,dirname)==0)
				{
					next_curdir=i;
					/*已经存在（已经打开），关闭当前目录直到跳转到打开的相应目录，返回*/
					while(curdir!=i)
					{
						curdir=do_close(curdir);
					}
					memset(currentdir,'\0',sizeof(currentdir));
					strcpy(currentdir,openfilelist[i].dir);
					return;
				}
			}
		}
		/*（2）获得若失败用的当前路径，关闭所有打开的文件，使得curdir最后指向根目录 */ 
		strcpy(recover_dir,currentdir);
		while(curdir)
		{
			curdir=do_close(curdir);
		}
		dir=strtok(NULL,"/");
	}
	/*（3）按路径不断逐级打开目录 */
	while(dir)
	{
	    strcpy(tdir,dir);
	    dir=strtok(NULL,"/");
		fd=do_open(tdir);
		if(fd!=-1)
		{
			curdir=fd;
			memset(currentdir,'\0',sizeof(currentdir));
			strcpy(currentdir,openfilelist[curdir].dir);
		}
		else
		{
			break;
		}

	}

	/*（4）若失败，按路径不断逐级关闭目录，并返回原目录（或未打开目录的 */
	if(fd==-1)
	{
		while(curdir)
		{
			curdir=do_close(curdir);
		}
		my_cd(recover_dir);
	}

}





/*在当前目录或者指定目录下创建目录，但是不打开*/
void my_mkdir(char *dirname)
{

	char recover_dir[80];
	char *dir,*exname,*path,*path1,*path2,*str;

	path=strtok(dirname,".");
	exname=strtok(NULL,".");
	if(!exname)
	{
		strcpy(recover_dir,currentdir);
		if(strcmp(recover_dir,"root")==0)
            printf("11111\n");//？？ 
        /*（1）直到获得纯目录名-path2，和其路径（不包括目录名本身）-path1*/ 
		path1=strtok(path,"/");
		path2=strtok(NULL,"/");
		str=strtok(NULL,"/");
		if(path2)//表示不是当前目录
		{
			while(str)
			{
				strcat(path1,path2);
				path2=str;
				str=strtok(NULL,"/");
			}
			my_cd(path1);
			do_mkdir(path2);
			/*（2）创建完重新回到原来的目录*/ 
			my_cd(recover_dir);
		}
		else//在当前目录中
		{
			do_mkdir(dirname);
		}

	}
	else
	{
		printf("Error:the dirname can have exname...\n");
		return;
	}

}


/*在当前目录下创建目录
1.返回值：1表示成功，0表示失败*/
void do_mkdir(char *dirname)
{
	int i;
	fat *fat1,*fatptr,*fat_bak,*fat_bakptr;
	fcb *fcbptr;
	int readsize;
	char text[MAXTEXTSIZE];
    time_t now;
    struct tm *nowtime;
	unsigned short blkno;
	int fd;

	/*(1)检查新建目录是否重名*/
	openfilelist[curdir].count=0;
	readsize=do_read(curdir,openfilelist[curdir].length,text);//将当前的目录文件读到内存中
	fcbptr=(fcb *)text;
	for(i=0;i<readsize/sizeof(fcb);i++)
	{
		if((fcbptr->free==1)&&(fcbptr->attribute==0)&&(strcmp(fcbptr->filename,dirname)==0))
		{
			printf("Error:the dir is already exist...\n");
			return ;
		}
		fcbptr++;
	}

	/*（2）修改当前目录文件(新目录的父目录），若有空fcb则获得其位置，否则新建一个FCB*/ 
	fcbptr=(fcb *)text;
	for(i=0;i<readsize/sizeof(fcb);i++)
	{
		if(fcbptr->free==0)
		{
			break;
		}
		fcbptr++;
	}
	/*（3）为新目录分配块，并修改fat表*/ 
	blkno=findblock();//申请新目录文件的第一个块号
	if(blkno==-1)
	{
		return ;
	}
	fat1=(fat *)(myvhard+BLOCKSIZE);
	fat_bak=(fat *)(myvhard+3*BLOCKSIZE);
	(fat1+blkno)->id=END;
	(fat_bak+blkno)->id=END;

	/*（4）新建一个fcb，填写新目录信息 */ 
    now = time(NULL);
    nowtime = localtime(&now);
    strcpy(fcbptr->filename,dirname);
    strcpy(fcbptr->exname," ");
    fcbptr->attribute=0;
    fcbptr->time = nowtime->tm_hour * 2048 + nowtime->tm_min * 32 + nowtime->tm_sec / 2;
    fcbptr->date = (nowtime->tm_year - 80) * 512 + (nowtime->tm_mon + 1) * 32 + nowtime->tm_mday;
    fcbptr->first=blkno;
    fcbptr->length=2*sizeof(fcb);
    fcbptr->free=1;
	//将新fcb放到新目录的父目录文件中去 
    openfilelist[curdir].count=i*sizeof(fcb);
    do_write(curdir,(char *)fcbptr,sizeof(fcb));

	/*(5.1)修改新建目录文件内容：添加一个当前目录的fcb（类型为目录，名为".") */
	fd=do_open(dirname);
	if(fd==-1)
	{
		return ;
	}
	fcbptr=(fcb *)malloc(sizeof(fcb));
    now = time(NULL);
    nowtime = localtime(&now);
    strcpy(fcbptr->filename, ".");
    strcpy(fcbptr->exname, " ");
    fcbptr->attribute=0;
    fcbptr->time=nowtime->tm_hour * 2048 + nowtime->tm_min * 32 + nowtime->tm_sec / 2;
    fcbptr->date = (nowtime->tm_year - 80) * 512 + (nowtime->tm_mon + 1) * 32 + nowtime->tm_mday;
    fcbptr->first = blkno;
    fcbptr->length = 2 * sizeof(fcb);
    fcbptr->free = 1;
    do_write(fd,(char *)fcbptr,sizeof(fcb));
    /*(5.2)修改新建目录文件内容：添加一个父目录的fcb（类型为目录，名为"..") */
    now = time(NULL);
    nowtime = localtime(&now);
    strcpy(fcbptr->filename, "..");
    strcpy(fcbptr->exname, " ");
    fcbptr->attribute=0;
    fcbptr->time=openfilelist[curdir].time;
    fcbptr->date =openfilelist[curdir].date;
    fcbptr->first =openfilelist[curdir].first;
    fcbptr->length =openfilelist[curdir].length;
    fcbptr->free = 1;
    do_write(fd,(char *)fcbptr,sizeof(fcb));
    free(fcbptr);
    do_close(fd);

    /*（6）当前目录文件（新目录的父目录）的长度改变的保存*/ 
    fcbptr=(fcb *)text;
    fcbptr->length=openfilelist[curdir].length;
    openfilelist[curdir].count=0;
    do_write(curdir,(char *)fcbptr,sizeof(fcb));//text?? 
    openfilelist[curdir].fcbstate=1;//dowirte只修改openfilelist不修改磁盘fcb？ 

    return ;
}



//当前目录要做的事情已经写完了。
/*删除目录（为空才会删除）*/
void my_rmdir(char *dirname)
{

	char recover_dir[80];
	char *dir,*exname,*path,*path1,*path2,*str;
	/*(1)若为.和..不进行删除操作*/
	if((strcmp(dirname,".")==0)||strcmp(dirname,"..")==0)
	{
	    printf("dirname:%s\n",dirname);
		printf("Error:can't remove this directory...\n");
		return ;
	}

	//判断是当前目录还是指定目录
	path=strtok(dirname,".");
	exname=strtok(NULL,".");
	/*（2）循环获得path1为目录路径，path2为目录名*/ 
	if(exname==NULL)//符合目录文件的命名规则
	{
		strcpy(recover_dir,currentdir);
		path1=strtok(path,"/");
		path2=strtok(NULL,"/");
		str=strtok(NULL,"/");
		if(path2)//表示不是当前目录
		{
			while(str)
			{
				strcat(path1,path2);
				path2=str;
				str=strtok(NULL,"/");
			}
			my_cd(path1);
			do_rmdir(path2);
			/*（3）删除完成，重新回到原来的目录*/ 
			my_cd(recover_dir);

		}
		else//在当前目录中
		{
			do_rmdir(dirname);
		}

	}
	else
	{
		printf("Error:the dirname can have exname...\n");
		return;
	}


}

/*删除当前目录下的目录（为空才可以）*/
void do_rmdir(char *dirname)
{
	int i,j;
	int readsize,t_readsize;
	char text[MAXTEXTSIZE],t_text[MAXTEXTSIZE];
	fcb *fcbptr,*t_fcbptr;
	fat *fatptr1,*fatptr2,*fat1,*fat2;
	int fd;
	char *dir,*exname,*path,*path1,*path2,*str;
	unsigned short blkno;

	/*（1）检查要删除的目录是否存在*/ 
	openfilelist[curdir].count=0;
	readsize=do_read(curdir,openfilelist[curdir].length,text);
	fcbptr=(fcb *)text;
	for(i=0;i<readsize/sizeof(fcb);i++)
	{
		if((fcbptr->free==1)&&(fcbptr->attribute==0)&&(strcmp(fcbptr->filename,dirname)==0))
		{
			break;
		}
		fcbptr++;
	}
	if(i==readsize/sizeof(fcb))
	{
		printf("Error:the directory is not exist...\n");
		return ;
	}

	/*（2）如果存在，检查其是否为空文件夹（即是否可以删除）*/ 
	fd=do_open(dirname);
	t_readsize=do_read(fd,openfilelist[fd].length,t_text);
	t_fcbptr=(fcb *)t_text;
	for(j=0;j<t_readsize/sizeof(fcb);j++)
	{
		if(t_fcbptr->free==1)
		{
			if(!(strcmp(t_fcbptr->filename,".")||strcmp(t_fcbptr->filename,".."))
			{
				printf("Error:the directory is not empty...\n");
				return;
			}
		}
		t_fcbptr++;
	}
	/*(3)如果为空，从fat表中回收文件的磁盘块*/
	blkno=openfilelist[fd].first;
	fat1=(fat *)(myvhard+BLOCKSIZE);
	fat2=(fat *)(myvhard+3*BLOCKSIZE);
	while(blkno!=END)
	{
		fatptr1=fat1+blkno;
		fatptr2=fat2+blkno;
		blkno=fatptr1->id;
		fatptr1->id=FREE;
		fatptr2->id=FREE;
	}
	do_close(fd);

	/*修改被删除目录的父目录内容里存的被删除目录的fcb*/ 
	fcbptr->free=0;
	strcpy(fcbptr->filename,"\0");
	strcpy(fcbptr->exname,"\0");
	openfilelist[curdir].count=i*sizeof(fcb);
	do_write(curdir,(char *)fcbptr,sizeof(fcb));
	openfilelist[curdir].fcbstate=1;
}

/*退出文件系统*/
void my_exitsys()
{
	FILE *fp;
	while(curdir)
	{
		curdir=do_close(curdir);
	}
	fp=fopen(myfilesys,"w");
	fwrite(myvhard,SIZE,1,fp);
	fclose(fp);
	free(myvhard);
}



int main()
{
    char cmd[15][10] = {"cd", "mkdir", "rmdir", "ls", "create", "rm", "open", "close", "write", "read", "format","exit"};
    char s[30], *sp;
	int cmdn,i;
	int ctn=1;
	int flag;
	
	/*申请虚拟磁盘空间*/
	myvhard=(unsigned char*)malloc(sizeof(char)*SIZE);
	if(myvhard == NULL)
	{
		printf("The application for the myvhard is err...\n");
		return;
	}
	memset(myvhard,'\0',SIZE);

    startsys();
    printf("*********************Welcome FAT filesystem*****************************\n\n");
    printf("command\t\tparam\t\tinstruction\n\n");
    printf("cd\t\tdirname/pathname\t\tgoto the dir\n");
    printf("mkdir\t\tdirname/pathname\t\t\tcreate a dir\n");
    printf("rmdir\t\tdirname/pathname\t\t\tdel a dir\n");
    printf("ls\t\t--\t\t\tshow the dir and files in curdir\n");
    printf("create\t\tfilename\t\t\tcreate the file in curdir\n");
    printf("rm\t\tfilename\t\t\tdel the file in curdir\n");
    printf("open\t\tfilename\t\t\topen the file in curdir\n");
    printf("write\t\t--\t\t\twrite the file only when it was opened\n");
    printf("read\t\t--\t\t\tread the file only when it was opened\n");
    printf("close\t\t--\t\t\tclose the file only when it was opened\n");
    printf("format\t\t--\t\t\tformat the file system\n");
    printf("exit\t\t--\t\t\texit the file system\n\n");
    printf("*********************************************************************\n\n");
    while(ctn)
    {
    	printf("%s>", currentdir);
    	gets(s);
    	strcat(s,"\0");
    	cmdn=-1;
    	if(strcmp(s,""))//如果不为空
    	{
    		sp=strtok(s," ");
    		for(i=0;i<15;i++)
    		{
    			if(strcmp(sp,cmd[i])==0)
    			{
    				cmdn=i;
    				break;
    			}
    		}
	    	switch(cmdn)
	    	{
	    		case 0:
	    		{
	    			sp=strtok(NULL," ");
	    			if(sp&&(openfilelist[curdir].attribute==0))
	    			{
	    				my_cd(sp);
	    			}
	    			else
	    			{
	    				printf("Error:the dir is not exist or the command is err...\n");
	    			}
	    			break;
	    		}
	    		case 1:
	    		{
	    			sp=strtok(NULL," ");
	    			if(sp&&(openfilelist[curdir].attribute==0))
	    			{
	    				my_mkdir(sp);
	    			}
	    			else
	    			{
	    				printf("Error:now is not in a dir or the command is err...\n");
	    			}
	    			break;
	    		}
	    		case 2:
	    		{
	    			sp=strtok(NULL," ");
	    			if(sp&&(openfilelist[curdir].attribute==0))
	    			{
	    				my_rmdir(sp);
	    			}
	    			else
	    			{
	    				printf("Error:now is not in a dir or the command is err...\n");
	    			}
	    			break;
	    		}
	    		case 3:
	    		{
//	    			sp=strtok(NULL," ");
	    			if(sp&&(openfilelist[curdir].attribute==0))
	    			{
	    				my_ls(sp);
	    			}
	    			else
	    			{
	    				printf("Error:now is not in a dir or the command is err...\n");
	    			}
	    			break;
	    		}
	    		case 4:
	    		{
	    			sp=strtok(NULL," ");
//	    			printf("%s",sp);
	    			if(sp&&(openfilelist[curdir].attribute==0))
	    			{
	    				my_create(sp);
	    			}
	    			else
	    			{
	    				printf("Error:now is not in a dir or the command is err...\n");
	    			}
	    			break;
	    		}
	    		case 5:
	    		{
	    			sp=strtok(NULL," ");
	    			if(sp&&(openfilelist[curdir].attribute==0))
	    			{
	    				my_rm(sp);
	    			}
	    			else
	    			{
	    				printf("Error:now is not in a dir or the command is err...\n");
	    			}
	    			break;
	    		}
	    		case 6:
	    		{
	    			sp=strtok(NULL," ");
	    			if(sp&&(openfilelist[curdir].attribute==0))
	    			{
	    				//如果这一步是打开，那么下一步必须是关闭，写，读等操作，不然不能执行其他操作
	    				flag=my_open(sp);
	    				if(flag!=-1)
	    				{
	    					curdir=flag;
	    				}
	    			}
	    			else
	    			{
	    				printf("Error:now is not in a dir or the command is err...\n");
	    			}
	    			break;
	    		}
	    		case 7:
	    		{
	    			if(openfilelist[curdir].attribute==1)
	    			{
	    				flag=my_close(curdir);//返回的是父目录的openfilelist的index
	    				if(flag!=-1)
	    				{
	    					curdir=flag;
	    				}
	    			}
	    			else
	    			{
	    				printf("Error:now is at a dir ,no file opened...\n");
	    			}
	    			break;
	    		}
	    		case 8:
	    		{
	    			if((openfilelist[curdir].attribute==1))
	    			{
	    				my_write(curdir);
	    			}
	    			else
	    			{
	    				printf("Error:now is at a dir ,no file opened...\n");
	    			}
	    			break;
	    		}
	    		case 9:
	    		{
	    			if((openfilelist[curdir].attribute==1))
	    			{
	    				my_read(curdir,openfilelist[curdir].length);
	    			}
	    			else
	    			{
	    				printf("Error:now is at a dir ,no file opened...\n");
	    			}
	    			break;
	    		}
	    		case 10:
	    		{
	    			my_format();
	    			startsys();
	    			break;
	    		}
	    		case 11:
	    		{
	    			if((openfilelist[curdir].attribute==0))
	    			{
	    				my_exitsys();
	    				ctn=0;
	    			}
	    			else
	    			{
	    				printf("Error:now is not in a dir...\n");
	    			}
	    			break;
	    		}
	    		default:
	    		{
	    			printf("Please input the right command...\n");
	    			break;
	    		}

	    	}
    	}
    }
    return 0;

