#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <net/net_namespace.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/random.h>

#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include "pktgene_cmd.h"

#define DBG_ZJ
//#undef DBG_ZJ
#define MTU_SIZE 1514
#define RC_SLEEEP /*sleep upon resource request failure*/
#ifdef DBG_ZJ
#define print_dbg(...) printk(__VA_ARGS__)
#else
#define print_dbg(...)
#endif

#define L2_PROTO_ARP 0x1
#define L2_PROTO_IP 0x2
#define L2_PROTO_UNKNOWN 0x0

#define L3_PROTO_ICMP 0x1
#define L3_PROTO_TCP  0x2
#define L3_PROTO_UDP  0x3
#define L3_PROTO_UNKNOWN 0x0

#define MAX_CPU_NO 32 /*suppose max cpu 32*/

int pktgene_worker(void* arg);
struct pktgene_task_item{//aligned task item,say the data-type is always 2 
	int frame_offset;
	union {	
		uint16_t word_val;
		unsigned char ustuff[2];//and here we use big-endian mem-like
	};
};

#define MAX_TASK_ITEM 64
struct pktgene_kthread_arg{
	int icpu;
	/*n to 1 mapping*/
	struct pktgene_task_item pti_arr[MAX_TASK_ITEM];
	int pti_no;
	
	struct net_device *ndev;
	netdev_tx_t (*xmit)(struct sk_buff*,struct net_device*);/*routine of packets tx*/
	struct pktgene_task_base *ptb_ptr;
	struct task_struct * ts_kthread;
	wait_queue_head_t wqh_kthread;
	/*status indicating how the kthread behaves*/
	int bruning:1;
	int bcanceld:1;/*when */
	int bready_to_reset:1;
	
};
struct pktgene_task_base{
	int task_base_no;
	unsigned char frame_base[MTU_SIZE+2];
	int frame_size;
	struct list_head list_node;
	int flags_csum:1;/*indicate whether to calc sum*/
	int max_frame_no;/*indicate how many frames will be issued*/
	atomic_t current_frame_no;
	struct list_head lh_task_line_head_node;
	spinlock_t tb_guard;
};

struct pktgene_task_line{/*we using little endian here*/
	int frame_start_no;
	int frame_end_no;/*-1 indicate never end*/
	int data_type;/*1,2,4 supported*/
	int frame_offset;
	int data_lseek;
	int data_acton;
	/*int data_length;*/
	union {
		uint32_t qword_val;
		uint16_t word_val;
		uint8_t  byte_val;
		unsigned char ustuff[4];
	};
	struct list_head lh_task_line_node;
};

struct pktgene_kthread_arg* alloc_pktgene_kthread_arg(void);
void free_pktgene_kthread_arg(struct pktgene_kthread_arg* arg);
struct pktgene_task_base * alloc_pktgene_task_base(void);
void free_pktgene_task_base(struct pktgene_task_base* base);
struct pktgene_task_base* duplicate_task_base(struct pktgene_task_base* src);

struct pktgene_task_line * alloc_pktgene_task_line(void);
void free_pktgene_task_line(struct pktgene_task_line * line);
void free_all_task_lines(struct pktgene_task_base * ptb);
struct pktgene_task_line* duplicate_task_line(struct pktgene_task_line * src);


struct sk_buff * generate_sk_buff(struct pktgene_kthread_arg*pka);
void decode_task_item(struct pktgene_kthread_arg*pka);
void recalculate_checksum(struct pktgene_kthread_arg *pka,unsigned char *base);



int pktgene_open(struct inode *inode,struct file*filp);
int pktgene_release(struct inode *inode,struct file*filp);
int pktgene_ioctl(struct inode*inode,struct file*filp,unsigned int cmd,unsigned long arg);

struct list_head* lh_remove(struct list_head * lh_head);





