#include "pktgene_main.h"


struct pktgene_kthread_arg* alloc_pktgene_kthread_arg(void)
{
	struct pktgene_kthread_arg * arg=NULL;
	arg=(struct pktgene_kthread_arg*)kzalloc(sizeof(struct pktgene_kthread_arg),GFP_KERNEL);
	return arg;
}
void free_pktgene_kthread_arg(struct pktgene_kthread_arg* arg)
{
	if(!arg)
		return;
	kfree(arg);
}
struct pktgene_task_base * alloc_pktgene_task_base(void)
{
	struct pktgene_task_base* ret;
	ret=(struct pktgene_task_base*)kzalloc(sizeof(struct pktgene_task_base),GFP_KERNEL);
	if(ret){
		INIT_LIST_HEAD(&ret->list_node);
		INIT_LIST_HEAD(&ret->lh_task_line_head_node);
		spin_lock_init(&ret->tb_guard);
		atomic_set(&ret->current_frame_no,0);
		
		ret->flags_csum=1;/*sum checking enabled */
	}
	return ret;
}

void free_pktgene_task_base(struct pktgene_task_base* base)
{
	if(base)
		kfree(base);
		
}
struct pktgene_task_base* duplicate_task_base(struct pktgene_task_base* src)
{
	struct pktgene_task_base* ret=alloc_pktgene_task_base();
	if(!ret)
		goto local_ret;
	ret->flags_csum=src->flags_csum;
	ret->frame_size=src->frame_size;
	memcpy(ret->frame_base,src->frame_base,src->frame_size);
	ret->task_base_no=src->task_base_no;
	ret->max_frame_no=src->max_frame_no;
	local_ret:
	return ret;
}
struct pktgene_task_line * alloc_pktgene_task_line(void)
{
	struct pktgene_task_line *ret;
	ret=(struct pktgene_task_line *)kzalloc(sizeof(struct pktgene_task_line),GFP_KERNEL);
	if(ret){
		INIT_LIST_HEAD(&ret->lh_task_line_node);
	}
	return ret;
}

void free_pktgene_task_line(struct pktgene_task_line * line)
{
	if(line)
		kfree(line);
}
struct pktgene_task_line* duplicate_task_line(struct pktgene_task_line * src)
{
	struct pktgene_task_line* ret;
	ret=alloc_pktgene_task_line();
	if(!ret)
		goto local_ret;
	ret->frame_start_no=src->frame_start_no;
	ret->frame_end_no=src->frame_end_no;
	ret->data_type=src->data_type;
	ret->frame_offset=src->frame_offset;
	ret->qword_val=src->qword_val;
	ret->data_acton=src->data_acton;
	ret->data_lseek=src->data_lseek;
	
	local_ret:
	return ret;
}

void free_all_task_lines(struct pktgene_task_base * ptb)
{
	struct list_head *lh_cur;
	struct pktgene_task_line* ptl_cur;
	while(!list_empty(&ptb->lh_task_line_head_node)){
		lh_cur=lh_remove(&ptb->lh_task_line_head_node);
		if(!lh_cur)
			break;
		print_dbg("\t>>>collect task line\n");
		ptl_cur=list_entry(lh_cur,struct pktgene_task_line,lh_task_line_node);
		free_pktgene_task_line(ptl_cur);
	}
}
struct list_head* lh_remove(struct list_head * lh_head)
{
	struct list_head* lh_ret=NULL;
	if(!list_empty(lh_head)){
		lh_ret=lh_head->next;
		list_del(lh_ret);/*we do not need __list_del actually*/
	}
	return lh_ret;
}
