#include "pktgene_main.h"

DECLARE_WAIT_QUEUE_HEAD(wqh_exit);
DECLARE_WAIT_QUEUE_HEAD(wqh_reset);

DECLARE_MUTEX(mut_exit);
int ifinishno=0;
int gcpuno;/*indicate number of cpus*/
struct pktgene_kthread_arg pka_arr[MAX_CPU_NO];
struct net_device * gndev=NULL;

int global_major=507;
dev_t devno;
struct cdev ifdev;
struct file_operations if_ops={
	.open=pktgene_open,
	.release=pktgene_release,
	.ioctl=pktgene_ioctl
};

struct list_head lh_task_base;
struct pktgene_task_base ptb_tmp;
struct pktgene_task_line ptl_tmp;

//////////////////////////////////////////////////
int __init pktgene_init(void)
{
	int cpu;
	int rc;
	/**********spawn per-cpu kthread*************/
	for_each_online_cpu(cpu){
		memset(&pka_arr[gcpuno],0x0,sizeof(struct pktgene_kthread_arg));
		pka_arr[gcpuno].icpu=cpu;
		init_waitqueue_head(&pka_arr[gcpuno].wqh_kthread);
		pka_arr[gcpuno].ts_kthread=kthread_create(pktgene_worker,&pka_arr[gcpuno],"pktgene_%d",cpu);
		if(pka_arr[gcpuno].ts_kthread){
			kthread_bind(pka_arr[gcpuno].ts_kthread,cpu);
			wake_up_process(pka_arr[gcpuno].ts_kthread);
			print_dbg(">>>create kthread_%d successfully\n",cpu);
			//////////////////////////////////////////////
		}
		else
			printk(">>>create kthread_%d fails\n",cpu);
		gcpuno++;
		
	}
	/***********setup char dev**************************/
	devno=MKDEV(global_major,0);
	rc=register_chrdev_region(devno,1,"pktgene");
	if(rc){
		printk(">>>can not register char region\n");
		goto local_ret;
	}
	cdev_init(&ifdev,&if_ops);
	cdev_add(&ifdev,devno,1);
	/*************setup other  data structure*********************/
	INIT_LIST_HEAD(&lh_task_base);


	#if 0
	struct pktgene_task_base *ptb=alloc_pktgene_task_base();
	struct pktgene_task_line * ptl=alloc_pktgene_task_line();

	list_add_tail(&(ptb->list_node),&lh_task_base);
	list_add_tail(&ptl->lh_task_line_node,&ptb->lh_task_line_head_node);

	ptb=alloc_pktgene_task_base();
	ptl=alloc_pktgene_task_line();
	list_add_tail(&(ptb->list_node),&lh_task_base);
	list_add_tail(&ptl->lh_task_line_node,&ptb->lh_task_line_head_node);
	#endif
	
	local_ret:
	return 0;
}
void __exit pktgene_exit(void)
{
	struct list_head *lh_cur;
	struct pktgene_task_base * ptb_cur;
	int idx=0;
	for(;idx<gcpuno;idx++){
		if(!pka_arr[idx].ts_kthread)
			continue;
		pka_arr[idx].bcanceld=1;/*set cancel-bit*/
		wake_up_interruptible(&pka_arr[idx].wqh_kthread);
	}
	wait_event(wqh_exit,ifinishno>=gcpuno);/*wait all worker finished*/

	/*********unset some data structure***********/
	/*make sure these data is no longer used*/
	/*free resource stack likely*/
	/*the following code snippet is reused later when module is reset*/
	while(!list_empty(&lh_task_base)){
		lh_cur=lh_remove(&lh_task_base);
		if(!lh_cur)
			continue;
		ptb_cur=list_entry(lh_cur,struct pktgene_task_base,list_node);
		
		/*furthermore ,deallocate some resource related*/

		print_dbg(">>>collect task base:%d\n",ptb_cur->task_base_no);
		free_all_task_lines(ptb_cur);
		free_pktgene_task_base(ptb_cur);
	}
	/*********del iterface dev**********/
	cdev_del(&ifdev);
	unregister_chrdev_region(devno,1);
}

int pktgene_worker(void* arg)
{
	struct sk_buff *skb;
	struct netdev_queue *txque;
	int queuemap;
	netdev_tx_t tx_ret;
	struct pktgene_kthread_arg *pka=(struct pktgene_kthread_arg*)arg;
	while(!kthread_should_stop()){
		if(!(pka->bcanceld||pka->bruning))
			wait_event_interruptible(pka->wqh_kthread,pka->bcanceld||pka->bruning);
		if(signal_pending(current)){/*I suppose here ,kthread is stopped,but never happened*/
			break;
		}
		
		if(pka->bcanceld)
			break;
		if(pka->bready_to_reset){
			pka->bruning=0;
			wake_up_interruptible(&wqh_reset);/*wake up reset ioctl*/
			continue;
		}
		/*do some stuff here*/
		#if 1
		if(atomic_read(&(pka->ptb_ptr->current_frame_no))>pka->ptb_ptr->max_frame_no){/*check frame no*/
			pka->bruning=0;
			print_dbg("...task finished\n");
			continue;
		}
		#endif
		if(!netif_running(pka->ndev)||!netif_carrier_ok(pka->ndev)){
			#ifdef RC_SLEEEP
			msleep(1);
			#endif
			
			continue;
		}
		skb=generate_sk_buff(pka);
		if(!skb){
			#ifdef RC_SLEEEP
			msleep(1);//delay for some resonable time
			#endif
			continue;
		}
		queuemap=skb_get_queue_mapping(skb);
		txque=netdev_get_tx_queue(pka->ndev,queuemap);
		if(!txque){/*suppose this will never happen if we handle queue mapping correctly in generation phaze*/
			kfree_skb(skb);
			continue;
		}
		__netif_tx_lock_bh(txque);
		tx_ret=(*pka->xmit)(skb,pka->ndev);

		switch(tx_ret)
		{
			case NETDEV_TX_OK:
				txq_trans_update(txque);
				atomic_inc(&pka->ptb_ptr->current_frame_no);
				break;
			default:
				kfree_skb(skb);
				break;
		}
		__netif_tx_unlock_bh(txque);
		
	}


	down(&mut_exit);
	ifinishno++;
	print_dbg(">>>worker %d finish\n",pka->icpu);
	up(&mut_exit);
	wake_up(&wqh_exit);
	return 0;
}
module_init(pktgene_init);
module_exit(pktgene_exit);


MODULE_AUTHOR("jzheng from bjtu.cit");
MODULE_LICENSE("Dual BSD/GPL");


int pktgene_open(struct inode *inode,struct file*filp)
{
	return 0;
}
int pktgene_release(struct inode *inode,struct file*filp)
{
	return 0;
}
int pktgene_ioctl(struct inode*inode,struct file*filp,unsigned int cmd,unsigned long arg)
{
	int rc;
	int itmp;
	struct list_head *lh_cur;
	struct pktgene_task_base *ptb_ptr;
	struct pktgene_task_line *ptl_ptr;
	int ibaseno;
	int icpuno;
	char if_name[MAX_IF_NAME];
	switch(cmd)
	{
		case PKTGENE_TEST:
			break;
		case PKTGENE_CMD_BASE_TASKBASE_NO:
			ptb_tmp.task_base_no=(int)arg;
			print_dbg(">>>ioctl_taskbase_no:%d\n",(int)arg);
			break;
		case PKTGENE_CMD_BASE_FRAMESIZE:
			ptb_tmp.frame_size=(int)arg;
			print_dbg(">>>ioctl_taskbase_framesize:%d\n",(int)arg);
			break;
		case PKTGENE_CMD_BASE_FRAME_BUFF:
			rc=access_ok(VERIFY_READ,arg,ptb_tmp.frame_size);
			if(!rc)
				return -EINVAL;
			rc=__copy_from_user(ptb_tmp.frame_base,(void*)arg,ptb_tmp.frame_size);
			print_dbg(">>>ioctl_taskbase_framebuff:%d\n",rc);
			break;
		case PKTGENE_CMD_BASE_CSUM:
			ptb_tmp.flags_csum=arg?1:0;
			print_dbg(">>>ioctl_taskbase_csum:%s\n",ptb_tmp.flags_csum?"enabled":"disabled");
			break;
		case PKTGENE_CMD_BASE_UPDTAE:
			ptb_ptr=duplicate_task_base(&ptb_tmp);
			if(!ptb_ptr)
				return -ENOTTY;
			list_add_tail(&ptb_ptr->list_node,&lh_task_base);
			print_dbg(">>>new task-base added:%d\n",ptb_ptr->task_base_no);
			break;
		case PKTGENE_CMD_LINE_START_NO:
			ptl_tmp.frame_start_no=(int)arg;
			print_dbg(">>>ioctl_taskline_startno:%d\n",ptl_tmp.frame_start_no);
			break;
		case PKTGENE_CMD_LINE_END_NO:
			ptl_tmp.frame_end_no=(int)arg;
			print_dbg(">>>ioctl_taskline_endno:%d\n",ptl_tmp.frame_end_no);
			break;
		case PKTGENE_CMD_LINE_DATA_TYPE:
			if(arg!=1&&arg!=2&&arg!=4)
				return -EINVAL;
			ptl_tmp.data_type=(int)arg;
			print_dbg(">>>ioctl_taskline_datatype:%d\n",ptl_tmp.data_type);
			break;
		case PKTGENE_CMD_LINE_FRAME_OFFSET:
			ptl_tmp.frame_offset=(int)arg;
			print_dbg(">>>ioctl_taskline_frame_offset:%d(0x%x)\n",ptl_tmp.frame_offset,ptl_tmp.frame_offset);
			break;
		case PKTGENE_CMD_LINE_CURRENT_VAL:
			switch(ptl_tmp.data_type)
			{
				case 1:
					ptl_tmp.byte_val=(int)arg;
					break;
				case 2:
					ptl_tmp.word_val=(int)arg;
					break;
				case 4:
					ptl_tmp.qword_val=(int)arg;
					break;
				default:
					return -ENOTTY;
			}
			print_dbg(">>>ioctl_taskline_curval:0x%x\n",ptl_tmp.qword_val);
			break;
		case PKTGENE_CMD_LINE_DATA_ACTION_AND_SEEK:
			itmp=(((int)arg)>>16)&0xffff;
			ibaseno=((int)arg)&0xffff;
			ptl_tmp.data_acton=itmp;
			ptl_tmp.data_lseek=ibaseno;
			print_dbg(">>>ioctl_taskline_dataaction(%d,%d)\n",ptl_tmp.data_acton,ptl_tmp.data_lseek);
			break;
		case PKTGENE_CMD_LINE_UPDATE:
			ptl_ptr=duplicate_task_line(&ptl_tmp);
			if(!ptl_ptr)
				return -ENOMEM;
			itmp=(int)arg;
			rc=0;
			list_for_each(lh_cur,&lh_task_base){
				ptb_ptr=list_entry(lh_cur,struct pktgene_task_base,list_node);
				if(ptb_ptr->task_base_no==itmp){
					rc=1;
					break;
				}
			}
			if(!rc){
				free_pktgene_task_line(ptl_ptr);
				return -EINVAL;
			}
			list_add_tail(&ptl_ptr->lh_task_line_node,&ptb_ptr->lh_task_line_head_node);
			print_dbg(">>>new task-line added:%d\n",ptb_ptr->task_base_no);
			break;
		case PKTGENE_CMD_BASE_ASSIGN:
			ibaseno=(((int)arg)>>16)&0xffff;
			icpuno=((int)arg)&0xffff;
			rc=0;
			list_for_each(lh_cur,&lh_task_base){
				ptb_ptr=list_entry(lh_cur,struct pktgene_task_base,list_node);
				if(ptb_ptr->task_base_no==ibaseno){
					rc=1;
					break;
				}
			}
			if(!rc)
				return -EINVAL;
			for(itmp=0;itmp<gcpuno;itmp++)
				if(pka_arr[itmp].icpu==icpuno)
					break;
			if(itmp==gcpuno)
				return -EINVAL;
			pka_arr[itmp].ptb_ptr=ptb_ptr;
			print_dbg(">>>task-base:%d is attached to cpu:%d\n",ptb_ptr->task_base_no,pka_arr[itmp].icpu);
			break;
		case PKTGENE_CMD_BASE_MAX_FRAME_NO:
			ptb_tmp.max_frame_no=(int)arg;
			print_dbg(">>>ioctl_taskbase_maxframeno:%d\n",ptb_tmp.max_frame_no);
			break;
		case PKTGENE_CMD_NETIF_GET_NETDEVICE:
			rc=access_ok(VERIFY_READ,arg,MAX_IF_NAME);
			if(!rc)
				return -EINVAL;
			__copy_from_user(if_name,(void*)arg,MAX_IF_NAME);
			gndev=dev_get_by_name(&init_net,if_name);
			if(!gndev)
				return -ENOTTY;
			print_dbg(">>>netdevice:%s found\n",if_name);
			break;
		case PKTGENE_CMD_NETIF_ATTACH_NETDEVICE:
			icpuno=(int)arg;
			for(itmp=0;itmp<gcpuno;itmp++){
				if(pka_arr[itmp].icpu==icpuno)
					break;
			}
			if(itmp==gcpuno)
				return -ENOTTY;
			pka_arr[itmp].ndev=gndev;
			pka_arr[itmp].xmit=gndev->netdev_ops->ndo_start_xmit;
			
			print_dbg(">>>attach ndev:%s to cpu:%d\n",gndev->name,pka_arr[itmp].icpu);
			/*printk("..........%d,%d\n",gndev->num_tx_queues,gndev->real_num_tx_queues);*/
			break;
		case PKTGENE_CMD_GLOBAL_SETRUNINGMASK:
			icpuno=(((int)arg)>>16)&0xffff;
			
			for(itmp=0;itmp<gcpuno;itmp++)
				if(pka_arr[itmp].icpu==icpuno)
					break;
			if(itmp==gcpuno)
				return -EINVAL;
			pka_arr[itmp].bruning=(((int)arg)&0xffff)?1:0;
			pka_arr[itmp].bready_to_reset=pka_arr[itmp].bruning?0:pka_arr[itmp].bready_to_reset;
			wake_up_interruptible(&pka_arr[itmp].wqh_kthread);
			print_dbg(">>>set cpu:%d runing-status %s\n",pka_arr[itmp].icpu,pka_arr[itmp].bruning?"enabled":"disabled");
			break;
		case PKTGENE_CMD_GLOBAL_RESET:
			for(itmp=0;itmp<gcpuno;itmp++)
				pka_arr[itmp].bready_to_reset=1;
			for(itmp=0;itmp<gcpuno;itmp++){
				if(pka_arr[itmp].bcanceld)continue;
				if(!pka_arr[itmp].bruning)continue;
				wait_event_interruptible(wqh_reset,!pka_arr[itmp].bruning||pka_arr[itmp].bcanceld);//in case module is uninstalled
			}
			/*reclaim  dynamicly allocated resource*/
			for(itmp=0;itmp<gcpuno;itmp++){
				pka_arr[itmp].ptb_ptr=NULL;
				pka_arr[itmp].ndev=NULL;
				pka_arr[itmp].xmit=NULL;
			}
			
			while(!list_empty(&lh_task_base)){
				lh_cur=lh_remove(&lh_task_base);
				if(!lh_cur)
					continue;
				ptb_ptr=list_entry(lh_cur,struct pktgene_task_base,list_node);
		
				/*furthermore ,deallocate some resource related*/

				print_dbg(">>>collect task base:%d\n",ptb_ptr->task_base_no);
				free_all_task_lines(ptb_ptr);
				free_pktgene_task_base(ptb_ptr);
			}
			break;
	}
	return 0;
}

struct sk_buff * generate_sk_buff(struct pktgene_kthread_arg*pka)
{
	struct ethhdr* eh;
	struct sk_buff *skb=NULL;
	if(!pka->ndev||!pka->ptb_ptr)
		goto local_ret;
	skb=__netdev_alloc_skb(pka->ndev,pka->ptb_ptr->frame_size+2+20,GFP_KERNEL);
	if(!skb)
		goto local_ret;
	skb_reserve(skb,2);/*for alignment of ethernet header 16 bytes*/
	eh=(struct ethhdr*)skb_put(skb,pka->ptb_ptr->frame_size);
	memcpy(eh,pka->ptb_ptr->frame_base,pka->ptb_ptr->frame_size);
	skb->protocol=htons(ETH_P_IP);
	skb->queue_mapping=0;/*assign 0 for simplicity,may be optimized later*/
	skb->dev=pka->ndev;
	skb->pkt_type=PACKET_HOST;
	/*alter some task lines later*/
	decode_task_item(pka);
	recalculate_checksum(pka,(unsigned char*)eh);
	local_ret:
	return skb;
}


void decode_task_item(struct pktgene_kthread_arg*pka)
{
	unsigned char  stuff[4];
	struct pktgene_task_base *ptb=pka->ptb_ptr;
	struct pktgene_task_line *ptl;
	struct list_head * lh_cur;
	int icur_frame_no;
	int imod;
	spin_lock(&ptb->tb_guard);
	icur_frame_no=atomic_read(&ptb->current_frame_no);
	if(icur_frame_no>ptb->max_frame_no)
		goto ret;
	pka->pti_no=0;//reset pti counter
	
	list_for_each(lh_cur,&ptb->lh_task_line_head_node){
		if(pka->pti_no>=MAX_TASK_ITEM)/*sanity check*/
			break;
		ptl=list_entry(lh_cur,struct pktgene_task_line,lh_task_line_node);
		if(icur_frame_no<ptl->frame_start_no)
			continue;
		if(ptl->frame_end_no!=-1&&icur_frame_no>ptl->frame_end_no)
			continue;
		if((ptl->frame_offset<0)||((ptl->frame_offset+ptl->data_type)>ptb->frame_size))
			continue;
		imod=ptl->frame_offset%2;//imod is Zero means this aligns very well,we do not pad or expand
		switch(ptl->data_type)
		{
			case 1:
				ptl->byte_val=(ptl->data_acton==DATA_ACTION_RANDOM)?(uint8_t)random32():(uint8_t)(ptl->byte_val+ptl->data_lseek);

				if(!imod){//aligned
					if(pka->pti_no>=MAX_TASK_ITEM)
						break;
					pka->pti_arr[pka->pti_no].frame_offset=ptl->frame_offset;
					pka->pti_arr[pka->pti_no].ustuff[0]=ptl->ustuff[0];
					pka->pti_arr[pka->pti_no].ustuff[1]=ptb->frame_base[ptl->frame_offset+1];
					pka->pti_no++;
				}else{
					if(pka->pti_no>=MAX_TASK_ITEM)
						break;
					pka->pti_arr[pka->pti_no].frame_offset=ptl->frame_offset-1;
					pka->pti_arr[pka->pti_no].ustuff[1]=ptl->ustuff[0];
					pka->pti_arr[pka->pti_no].ustuff[0]=ptb->frame_base[ptl->frame_offset-1];
					pka->pti_no++;
				}
				
				break;
			case 2:
				ptl->word_val=(ptl->data_acton==DATA_ACTION_RANDOM)?(uint16_t)random32():(uint16_t)(ptl->word_val+ptl->data_lseek);
				*((uint16_t*)stuff)=htons(ptl->word_val);//adjust byte order
				if(!imod){
					if(pka->pti_no>=MAX_TASK_ITEM)break;
						
					pka->pti_arr[pka->pti_no].frame_offset=ptl->frame_offset;
					pka->pti_arr[pka->pti_no].ustuff[0]=stuff[0];
					pka->pti_arr[pka->pti_no].ustuff[1]=stuff[1];
					pka->pti_no++;
				}else{
					if(pka->pti_no>=MAX_TASK_ITEM)break;
					pka->pti_arr[pka->pti_no].frame_offset=ptl->frame_offset-1;
					pka->pti_arr[pka->pti_no].ustuff[1]=stuff[0];
					pka->pti_arr[pka->pti_no].ustuff[0]=ptb->frame_base[ptl->frame_offset-1];
					pka->pti_no++;

					if(pka->pti_no>=MAX_TASK_ITEM)break;
					pka->pti_arr[pka->pti_no].frame_offset=ptl->frame_offset+1;
					pka->pti_arr[pka->pti_no].ustuff[0]=stuff[1];
					pka->pti_arr[pka->pti_no].ustuff[1]=ptb->frame_base[ptl->frame_offset+2];
					pka->pti_no++;
				}
				
				break;
			case 4:
				ptl->qword_val=(ptl->data_acton==DATA_ACTION_RANDOM)?(uint32_t)random32():(uint32_t)(ptl->qword_val+ptl->data_lseek);
				*((uint32_t*)stuff)=htonl(ptl->qword_val);
				if(!imod){
					if(pka->pti_no>=MAX_TASK_ITEM)break;
					pka->pti_arr[pka->pti_no].frame_offset=ptl->frame_offset;
					pka->pti_arr[pka->pti_no].ustuff[0]=stuff[0];
					pka->pti_arr[pka->pti_no].ustuff[1]=stuff[1];
					pka->pti_no++;
					if(pka->pti_no>=MAX_TASK_ITEM)break;
					pka->pti_arr[pka->pti_no].frame_offset=ptl->frame_offset+2;
					pka->pti_arr[pka->pti_no].ustuff[0]=stuff[2];
					pka->pti_arr[pka->pti_no].ustuff[1]=stuff[3];
					pka->pti_no++;
				}else{
					if(pka->pti_no>=MAX_TASK_ITEM)break;
					pka->pti_arr[pka->pti_no].frame_offset=ptl->frame_offset-1;
					pka->pti_arr[pka->pti_no].ustuff[0]=ptb->frame_base[ptl->frame_offset-1];
					pka->pti_arr[pka->pti_no].ustuff[1]=stuff[0];
					pka->pti_no++;
					if(pka->pti_no>=MAX_TASK_ITEM)break;
					pka->pti_arr[pka->pti_no].frame_offset=ptl->frame_offset+1;
					pka->pti_arr[pka->pti_no].ustuff[0]=stuff[1];
					pka->pti_arr[pka->pti_no].ustuff[1]=stuff[2];
					pka->pti_no++;
					if(pka->pti_no>=MAX_TASK_ITEM)break;
					pka->pti_arr[pka->pti_no].frame_offset=ptl->frame_offset+3;
					pka->pti_arr[pka->pti_no].ustuff[0]=stuff[3];
					pka->pti_arr[pka->pti_no].ustuff[1]=ptb->frame_base[ptl->frame_offset+4];
					pka->pti_no++;
				}
				break;
			default:
				continue;
				break;
		}
		
	}
	ret:
	spin_unlock(&ptb->tb_guard);
}
uint16_t wrap_csum(uint16_t oldsum,uint16_t oldval,uint16_t newval)
{
	uint32_t newsum=((0xffff)&(~oldsum));
	newsum+=((0xffff)&(~oldval))+newval;
	while(newsum&0xffff0000)
		newsum=(newsum&0xffff)+((newsum>>16)&0xffff);
	return (uint16_t)((~newsum)&0xffff);
}
void recalculate_checksum(struct pktgene_kthread_arg *pka,unsigned char *base)
{
	int idx=0;
	unsigned char * loffset;
	uint16_t oldval,newval;
	struct tcphdr * tcpptr=NULL;
	struct iphdr  * ipptr=NULL;
	struct udphdr * udpptr=NULL;
	struct icmphdr * icmpptr=NULL;
	struct arphdr  *  arpptr=NULL;
	struct ethhdr *ehptr=(struct ethhdr*)base;
	int L2_proto=L2_PROTO_UNKNOWN;
	int L3_proto=L3_PROTO_UNKNOWN;
	switch(htons(ehptr->h_proto))
	{
		case 0x0806:
			L2_proto=L2_PROTO_ARP;
			arpptr=(struct arphdr*)(sizeof(struct ethhdr)+(unsigned char*)ehptr);
			break;
		case 0x0800:
			L2_proto=L2_PROTO_IP;
			ipptr=(struct iphdr*)(sizeof(struct ethhdr)+(unsigned char*)ehptr);
			break;
		default:
			L2_proto=L2_PROTO_UNKNOWN;
			break;
	}

	switch(L2_proto)
	{
		case L2_PROTO_UNKNOWN:
			//return;
			break;
		case L2_PROTO_IP:
			switch(ipptr->protocol)
			{
				case 0x06:
					L3_proto=L3_PROTO_TCP;
					tcpptr=(struct tcphdr*)(ipptr->ihl*4+(unsigned char*)ipptr);
					break;
				case 0x11:
					L3_proto=L3_PROTO_UDP;
					udpptr=(struct udphdr*)(ipptr->ihl*4+(unsigned char*)ipptr);
					break;
				case 0x1:
					L3_proto=L3_PROTO_ICMP;
					icmpptr=(struct icmphdr*)(ipptr->ihl*4+(unsigned char*)ipptr);
					break;
				default:
					L3_proto=L3_PROTO_UNKNOWN;
					break;
			}
			break;
	}
	
	for(idx=0;idx<pka->pti_no;idx++){
		oldval=*(uint16_t*)(base+pka->pti_arr[idx].frame_offset);
		newval=pka->pti_arr[idx].word_val;
		loffset=base+pka->pti_arr[idx].frame_offset;
		if(L2_proto==L2_PROTO_IP ){//ip check-sum only 
			if(loffset==(unsigned char*)&ipptr->check)continue;//skip ip check-sum field 
			if((loffset>=(unsigned char*)ipptr)&&(loffset<(ipptr->ihl*4+(unsigned char*)ipptr)))//recalculate ip header checksum if possible
				ipptr->check=wrap_csum(ipptr->check,oldval,newval);
			
			if(L3_proto==L3_PROTO_TCP){
				if(loffset==(unsigned char*)&tcpptr->check)continue;//skip tcp check-sum field 
				if(((loffset>=(unsigned char*)&ipptr->saddr)&&(loffset<(8+(unsigned char*)&ipptr->saddr)))||(loffset>=(unsigned char*)tcpptr))
					tcpptr->check=wrap_csum(tcpptr->check,oldval,newval);//recalculate and update tcp check sum if presudoheader or tcp header and tcp payload is changed
					
			}else if(L3_proto==L3_PROTO_UDP){
				if(loffset==(unsigned char*)&udpptr->check)continue;//skip udp check-sum field
				if((((loffset>=(unsigned char*)&ipptr->saddr)&&(loffset<(8+(unsigned char*)&ipptr->saddr)))||(loffset>=(unsigned char*)udpptr))&&(udpptr->check))
					udpptr->check=wrap_csum(udpptr->check,oldval,newval);
			}
		}
		*(uint16_t*)(base+pka->pti_arr[idx].frame_offset)=newval;//update the appropriate value
	}
	
}

