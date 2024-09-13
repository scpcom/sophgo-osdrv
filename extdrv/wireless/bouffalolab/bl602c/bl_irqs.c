/**
 ******************************************************************************
 *
 *  @file bl_irqs.c
 *
 *  Copyright (C) BouffaloLab 2017-2024
 *
 *  Licensed under the Apache License, Version 2.0 (the License);
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an ASIS BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************
 */

#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/timex.h>
#include <linux/rtc.h>

#include "bl_defs.h"
#include "bl_cmds.h"
#include "ipc_host.h"
#include "bl_sdio.h"
#include "bl_v7.h"

int bl_main_process(struct bl_hw *bl_hw);


void bl_queue_main_work(struct bl_hw *bl_hw)
{
	BL_DBG(BL_FN_ENTRY_STR);

    if(bl_hw->surprise_removed)
        return;

	spin_lock_irq(&bl_hw->main_proc_lock);
	if (bl_hw->bl_processing) {
		BL_DBG("bl_queue_main_work: set more_task_flag true\n");
		bl_hw->more_task_flag = true;
		spin_unlock_irq(&bl_hw->main_proc_lock);
	} else {
		spin_unlock_irq(&bl_hw->main_proc_lock);
		BL_DBG("bl_queue_main_work: schedule work\n");
		queue_work(bl_hw->workqueue, &bl_hw->main_work);
	}
}


/*
 * This function decodes a received packet.
 *
 * Based on the type, the packet is treated as either a data, or
 * a command response, or an event, and the correct handler
 * function is invoked.
 */
static int bl_decode_rx_packet(struct bl_hw *bl_hw, struct sk_buff *skb, u32 upld_type, u32 hw_idx, u32 port)
{
	unsigned long now = jiffies;
	void *hostid = bl_hw->ipc_env->msga2e_hostid;
	u32 pld_len = le16_to_cpu(*(__le16 *) (skb->data));
#ifndef  BL_RX_REORDER
	struct bl_agg_reodr_msg *agg_reord_msg = NULL;
	struct bl_agg_reord_pkt *reord_pkt = NULL;
	struct bl_agg_reord_pkt *reord_pkt_tmp = NULL;
	bool found = false;
	u8 count = 0;
	u16 sn_copy = 0;
#endif
	u16 pad_len = 0;
	if(port !=0) {
		pad_len = le16_to_cpu(*(__le16 *) (skb->data + 6)) & 0xF;
		BL_DBG("pad_len=%d\n", pad_len);
	}

	//skb_pull(skb, sizeof(struct sdio_hdr));
	skb_pull(skb, sizeof(struct sdio_hdr)+pad_len);

	switch (upld_type) {
	case BL_TYPE_DATA:
		BL_DBG("info: -------------------------------------------------------->>>Rx: Data packet\n");
#ifdef BL_RX_REORDER
		//spin_lock_irq(&bl_hw->rx_lock);
        	skb_queue_tail(&bl_hw->rx_pkt_list, skb);
		bl_hw->rx_pkt_cnt ++;
		//spin_unlock_irq(&bl_hw->rx_lock);
		
		spin_lock_irq(&bl_hw->rx_process_lock);
		if(bl_hw->rx_processing){
			bl_hw->more_rx_task_flag = true;
			spin_unlock_irq(&bl_hw->rx_process_lock);
		} else {
			spin_unlock_irq(&bl_hw->rx_process_lock);
			queue_work(bl_hw->rx_workqueue, &bl_hw->rx_work);
		}
#else
		bl_rxdataind(bl_hw, skb);
#endif
		bl_hw->stats.last_rx = now;
		break;
	
	case BL_TYPE_ACK:
		BL_DBG("info: -------------------------------------------------------->>>Rx: ACK  packet\n");
		BL_DBG("Receive ACK: msg_cnt=%u, src_id=%u\n", bl_hw->ipc_env->msga2e_cnt, ((*(u8 *)(skb->data)) & 0xFF));
		/*just msg_cnt is matched with src_id or not*/
   		ASSERT_ERR(hostid);
		if(!hostid){

		   bl_hw->ipc_env->msga2e_cnt++;
           if (skb != bl_hw->msg_skb ) 
		       dev_kfree_skb_any(skb);
		   break;
		}
        ASSERT_ERR(bl_hw->ipc_env->msga2e_cnt == ((*(u8 *)(skb->data)) & 0xFF));
        if (bl_hw->ipc_env->msga2e_cnt != ((*(u8 *)(skb->data)) & 0xFF)){			
			//printk("Receive ACK: msg_cnt=%u, src_id=%u\n", bl_hw->ipc_env->msga2e_cnt, ((*(u8 *)(skb->data)) & 0xFF));
			bl_hw->ipc_env->msga2e_cnt = ((*(u8 *)(skb->data)) & 0xFF);
		}
		
   		bl_hw->ipc_env->msga2e_hostid = NULL;
		bl_hw->ipc_env->msga2e_cnt++;
		bl_hw->ipc_env->cb.recv_msgack_ind(bl_hw, hostid);		
        if (skb != bl_hw->msg_skb )
            dev_kfree_skb_any(skb);
		break;

	case BL_TYPE_MSG:
		BL_DBG("info: -------------------------------------------------------->>>Rx: MSG  packet\n");
		bl_hw->ipc_env->cb.recv_msg_ind(bl_hw, (void *)(skb->data));
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;
		
	case BL_TYPE_DBG_DUMP_START:
		BL_DBG("info: -------------------------------------------------------->>>Rx: DBG_DUMP_START packet\n");
		if(!(bl_hw->dbg_dump_start)) {
			BL_DBG("dbg_dump start now...\n");
			bl_hw->dbg_dump_start = true;
			bl_hw->la_buf_idx = 0;
		} else {
			BL_DBG("dbg_dump has not completed!\n");
		}
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;

	case BL_TYPE_DBG_DUMP_END:
		BL_DBG("info: -------------------------------------------------------->>>Rx: DBG_DUMP_END packet\n");
		bl_hw->dbg_dump_start = false;
		BL_DBG("%s\n", (char *)(bl_hw->dbginfo.buf->dbg_info.error));
		//TODO: wake app to cat mactrace
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;

	case BL_TYPE_DBG_LA_TRACE:
		BL_DBG("info: -------------------------------------------------------->>>Rx: DBG_LA_TRACE packet\n");
#ifdef CONFIG_BL_DBG
		memcpy(&(bl_hw->dbginfo.buf->la_mem[bl_hw->la_buf_idx]), skb->data, pld_len);
		bl_hw->la_buf_idx = bl_hw->la_buf_idx + pld_len;
#endif
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;

	case BL_TYPE_DBG_RBD_DESC:
		BL_DBG("info: -------------------------------------------------------->>>Rx: DBG_RBD_DESC packet\n");
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;

	case BL_TYPE_DBG_RHD_DESC:
		BL_DBG("info: -------------------------------------------------------->>>Rx: DBG_RHD_DESC packet\n");
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;

	case BL_TYPE_DBG_TX_DESC:
		BL_DBG("info: -------------------------------------------------------->>>Rx: DBG_TX_DESC packet\n");
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;
		
	case BL_TYPE_DUMP_INFO:
		BL_DBG("info: -------------------------------------------------------->>>Rx: DUMP_INFO packet\n");
		memcpy(&(bl_hw->dbginfo.buf->dbg_info), skb->data, sizeof(struct dbg_debug_info_tag));
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;
		
	case BL_TYPE_TXCFM:
		BL_DBG("info: -------------------------------------------------------->>>Rx:TXCFM packet\n");
		{
			u32 i;
			u32 count;
			struct bl_hw_txcfm hw_hdr;
			struct bl_txq *txq = NULL;


			
			/* 1. get cfm count from skb->data */
			memcpy(&hw_hdr, (u8 *)(skb->data), sizeof(struct bl_hw_txcfm));
			count = hw_hdr.cfm.count;
			BL_DBG("cfm.sn=%u, cfm.timestamp=%u, cfm.count=%u, cfm.credits=%d, staid=%d, tid=%d\n", hw_hdr.cfm.sn, hw_hdr.cfm.timestamp, hw_hdr.cfm.count, hw_hdr.cfm.credits, hw_hdr.staid, hw_hdr.tid);
			
			/* 2. handle cfm successively */
			for(i = 0; i < count; i++) {
				ipc_host_tx_cfm_handler(bl_hw->ipc_env, hw_idx, 0, &hw_hdr, &txq);
				if(!txq)
					goto TXCFM_EXIT;
				if((hw_hdr.cfm.status.retry_required || hw_hdr.cfm.status.sw_retry_required) && (count == 1)) {
					BL_DBG("Retry packet received, so we just exit\n");
					goto RETRY_PACKET;
				}
			}

			//BL_DBG("get txq from cfm_handler: txq->idx=%d\n", txq->idx);
#ifdef BL_FW_RETRY
			goto TXCFM_EXIT;
#endif

			/* 3. update txq->credits */
		    if (txq->idx != TXQ_INACTIVE) {
		        if (hw_hdr.cfm.credits) {
					BL_DBG("orign txq->idx=%d, txq->status=0x%x, txq->credits=%d\n", txq->idx, txq->status, txq->credits);
		            txq->credits += hw_hdr.cfm.credits;
					BL_DBG("after add credits: hw_hdr.cfm.credits=%d, txq->idx=%d, txq->status=0x%x, txq->credits=%d\n",
									hw_hdr.cfm.credits, txq->idx, txq->status, txq->credits);

				/*	spin_lock_bh(&bl_hw->txq_lock);
		            if (txq->credits <= 0)
		                bl_txq_stop(txq, BL_TXQ_STOP_FULL);
		            else if (txq->credits > 0)
		                bl_txq_start(txq, BL_TXQ_STOP_FULL);
					spin_unlock_bh(&bl_hw->txq_lock);*/
		        }

		        /* continue service period */
		        if (unlikely(txq->push_limit && !bl_txq_is_full(txq))) {
                    /* aline with bl_txq_queue_skb and bl_tx_retry, we use txq_lock here */
                    spin_lock_bh(&bl_hw->txq_lock);
		            bl_txq_add_to_hw_list(txq);
                    spin_unlock_bh(&bl_hw->txq_lock);
		        }
		    }

			/* 4. update ampdu_size */
			if (hw_hdr.cfm.ampdu_size &&
	        	hw_hdr.cfm.ampdu_size < IEEE80211_MAX_AMPDU_BUF)
	        	bl_hw->stats.ampdus_tx[hw_hdr.cfm.ampdu_size - 1]++;

			/* 5. update amsdu_size */
		#ifdef CONFIG_BL_AMSDUS_TX
		    txq->amsdu_len = hw_hdr.cfm.amsdu_size;
		#endif

		}
        
		bl_hw->stats.last_tx = now;
TXCFM_EXIT:
RETRY_PACKET:	
		bl_hw->data_sent = false;
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;

	case BL_TYPE_AGG_REORD_MSG:
		BL_DBG("info: -------------------------------------------------------->>>Rx:AGG REORDER MSG packet\n");
#ifndef  BL_RX_REORDER
		agg_reord_msg = (struct bl_agg_reodr_msg *)(skb->data);

		sn_copy = agg_reord_msg->sn;

		BL_DBG("AGG_REORD_MSG: sn=%u, num=%u, sta_idx=%d, tid=%d, status=%d\n",
				agg_reord_msg->sn, agg_reord_msg->num, agg_reord_msg->sta_idx, agg_reord_msg->tid, agg_reord_msg->status);

RE_THOUGH_LIST:
		/*1. get the skb according sn*/
		if (!list_empty(&bl_hw->reorder_list[agg_reord_msg->sta_idx*NX_NB_TID_PER_STA + agg_reord_msg->tid])) {
				list_for_each_entry_safe(reord_pkt, reord_pkt_tmp,  &bl_hw->reorder_list[agg_reord_msg->sta_idx*NX_NB_TID_PER_STA + agg_reord_msg->tid], list) {
					BL_DBG("reord_pkt->sn=%u\n", reord_pkt->sn);
					if((reord_pkt->sn == agg_reord_msg->sn) && (count < agg_reord_msg->num)) {
						if(!found)
							found = true;

						BL_DBG("found pkt: sn=%u, next_sn=%u, count(%u) of num(%u)\n", 
								agg_reord_msg->sn, (agg_reord_msg->sn + 1) % AGG_REORD_MSG_MAX_NUM, count, agg_reord_msg->num);
						/*2. modify the status in skb according agg_reodr_msg->status*/
						memcpy(reord_pkt->skb->data, &(agg_reord_msg->status), 1);
						/*3. call bl_rxdataind to handle the skb*/
						bl_rxdataind(bl_hw, reord_pkt->skb);
						/*4. delete this agg reodr pkt node*/
						list_del(&reord_pkt->list);
						kmem_cache_free(bl_hw->agg_reodr_pkt_cache, reord_pkt);
						/*5. find next pkt, if sn == 4095, next sn will change to 0*/
						agg_reord_msg->sn = (agg_reord_msg->sn + 1) % AGG_REORD_MSG_MAX_NUM;
						count++;
					}
						
					BL_DBG("count=%u, agg_reord_msg->num=%u\n", count, agg_reord_msg->num);

					if((agg_reord_msg->sn == 0) && (count < agg_reord_msg->num)) {
							BL_DBG("sn=0, re-though the list\n");
							goto RE_THOUGH_LIST;
					}

					if(count == agg_reord_msg->num) {
						count = 0;
						agg_reord_msg->sn = 0;
						break;
					}
				}
				if(!found)
					BL_DBG("Not found the packet, sn=%u\n", agg_reord_msg->sn);
		} else {
			BL_DBG("Oh! give me reodr msg, but no packet need to handle?\n");
		}
#endif
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;

	case BL_TYPE_DBG:
		BL_DBG("info: -------------------------------------------------------->>>Rx:DBG MSG packet\n");
		*(skb->data+pld_len) = '\0';
		printk("fw: %s", (char *)(skb->data));
        if (strstr((char *)(skb->data), "***MAC Signed:")) {
            extern char fw_signed[];
            char *build_str = strstr((char *)skb->data, "***MAC Signed:");
            if (build_str)
                strcpy(fw_signed, build_str+strlen("***MAC Signed:"));
        }
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;	

	case BL_TYPE_TX_STOP:
		BL_DBG("info: -------------------------------------------------------->>>Rx:TX_STOP packet\n");
		bl_hw->recovery_flag = true;
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;

	case BL_TYPE_TX_RESUME:
		BL_DBG("info: -------------------------------------------------------->>>Rx:TX_RESUME packet\n");
		bl_hw->recovery_flag = false;
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;
		
	default:
		BL_DBG("info: -------------------------------------------------------->>>Rx:UNKNOWN[%#x] packet\n", upld_type);
		if (skb != bl_hw->msg_skb )
		    dev_kfree_skb_any(skb);
		break;
	}

	return 0;
}

#if 1
/*
 * This function receive data from the sdio device.
 */
static int bl_card_to_host(struct bl_hw *bl_hw, u32 *type, u32 *hw_idx, u8 *buffer, u32 npayload, u32 ioport)
{
	int ret;
	u32 nb;
	int reserved;
	u8 rd_bitmap_l = 0;
	u8 rd_bitmap_u = 0;
	u32 bitmap;
	u32 bitmap_cur;
	u32 pre_rdport = 0;
	int count = 0;
	struct bl_device *bl_device;
	struct bl_plat *bl_plat;

	bl_plat = bl_hw->plat;
	bl_device = (struct bl_device *)bl_plat->priv;

	if (!buffer) {
		printk("%s: buffer is NULL\n", __func__);
		return -1;
	}
	
	ret = bl_read_data_sync(bl_hw, buffer, npayload, ioport);

	if (ret) {
		printk("%s: read iomem failed: %d\n", __func__, ret);
		return -1;
	} else {
		bl_read_reg(bl_hw, bl_device->reg->rd_bitmap_l, &rd_bitmap_l);
		bl_read_reg(bl_hw, bl_device->reg->rd_bitmap_u, &rd_bitmap_u);
		bitmap = rd_bitmap_l;
		bitmap |= rd_bitmap_u << 8;

		bitmap_cur = bitmap;

		while(bitmap) {
			bitmap = bitmap & (bitmap-1);
			count++;
		}

		pre_rdport = ioport - bl_hw->plat->io_port;
		BL_DBG("read success: bitmap=0x%x, pre_rdport=%d, 1<<pre_rdport=0x%x, count=%d\n", bitmap_cur, pre_rdport, 1<<pre_rdport, count);

		if((bitmap_cur & (1<<pre_rdport)) && (count>1)) {
			BL_DBG("read success, but port bitmap was not clear\n");
		}
	}

	*type = le16_to_cpu(*(__le16 *) (buffer + 2));
	nb = le16_to_cpu(*(__le16 *) (buffer));
	*hw_idx = le16_to_cpu(*(__le16 *) (buffer + 4));
	reserved = le16_to_cpu(*(__le16 *) (buffer + 6));
		
	return ret;
}

/*
 * This function transfers received packets from card to driver, performing
 * aggregation if required.
 *
 * For data received on control port, or if aggregation is disabled, the
 * received buffers are uploaded as separate packets. However, if aggregation
 * is enabled and required, the buffers are copied onto an aggregation buffer,
 * provided there is space left, processed and finally uploaded.
 */
static int bl_rx_process(struct bl_hw *bl_hw, struct sk_buff *skb, u32 port)
{
	u32 pkt_type;
	u32 hw_idx;
	u16 *buf;

	if (bl_card_to_host(bl_hw, &pkt_type, &hw_idx, skb->data, skb->len, bl_hw->plat->io_port + port))
			goto error;

	buf = (u16 *)(skb->data);
	BL_DBG("buf[0]=%u, buf[1]=%u, buf[2]=%u, buf[3]=%u, bl_hw->msg_idx=%u\n", buf[0], buf[1], buf[2], buf[3], bl_hw->msg_idx);
	if(port == 0) {
		if (bl_hw->msg_idx == buf[3]) {
			BL_DBG("receive msg twice, msg_idx=%u\n", bl_hw->msg_idx);
			return 0;
		}
	}

	//update msg idx
	bl_hw->msg_idx = buf[3];

	bl_decode_rx_packet(bl_hw, skb, pkt_type, hw_idx, port);

	return 0;

error:
	dev_kfree_skb_any(skb);
	return -1;
}
#endif

void bl_get_interrupt_status(struct bl_hw *bl_hw, u8 claim)
{
	struct bl_device *bl_device;
	struct bl_plat *bl_plat;
	u8 sdio_ireg;
	u8 rd_bitmap_l = 0;
	u8 rd_bitmap_u = 0;
	u8 wr_bitmap_l = 0;
	u8 wr_bitmap_u = 0;
	u32 bitmap;
#ifdef BL_INT_WRITE_CLEAR	
	u8 wclr_sdio_ireg;
#endif	

	static u32  up_cnt=0;
	
	static u32  down_cnt=0;

	bl_plat = bl_hw->plat;
	bl_device = (struct bl_device *)bl_plat->priv;

    if(claim){
        if(bl_read_data_sync(bl_hw, bl_plat->mp_regs, bl_device->reg->max_mp_regs, REG_PORT/* | BL_SDIO_BYTE_MODE_MASK*/)) {
            printk("[WD]read mp_regs failed\n");
            return;
        }
    } else {
        if(bl_read_data_sync_claim0(bl_hw, bl_plat->mp_regs, bl_device->reg->max_mp_regs, REG_PORT/* | BL_SDIO_BYTE_MODE_MASK*/)) {
            printk("read mp_regs failed\n");
            return;
        }
    }


	sdio_ireg = bl_plat->mp_regs[bl_device->reg->host_int_status_reg];

#ifdef BL_INT_WRITE_CLEAR	
	wclr_sdio_ireg = (~sdio_ireg) & 0x3f;
	if (sdio_ireg > 0) {
		if(claim){
			if(bl_write_reg(bl_hw, bl_device->reg->host_int_status_reg, wclr_sdio_ireg)) {
				printk("[WD]write int_status_reg failed\n");
				return;
			}
	    } 
		else {
			if(bl_write_reg_claim0(bl_hw, bl_device->reg->host_int_status_reg, wclr_sdio_ireg)) {
				printk("write int_status_reg failed\n");
				return;
			}
		}
	}
#endif

    if(claim && !bl_plat->mp_rd_bitmap && !(sdio_ireg & UP_LD_HOST_INT_STATUS)){
	    rd_bitmap_l = bl_plat->mp_regs[bl_device->reg->rd_bitmap_l];
	    rd_bitmap_u = bl_plat->mp_regs[bl_device->reg->rd_bitmap_u];
	    bitmap = rd_bitmap_l;
	    bitmap |= rd_bitmap_u << 8;
		if(bitmap){
			up_cnt++;
            BL_DBG("U %d, bitmap=0x%x, mp_rb=0x%x, cur_rp=%d\n", up_cnt, bitmap, bl_plat->mp_rd_bitmap, bl_hw->plat->curr_rd_port);
			sdio_ireg |=UP_LD_HOST_INT_STATUS;
		}

	}
	
    if(!(bl_plat->mp_wr_bitmap & 0xfffe) && !(sdio_ireg & DN_LD_HOST_INT_STATUS) && !bl_hw->data_sent && claim){
	    wr_bitmap_l = bl_plat->mp_regs[bl_device->reg->wr_bitmap_l];
	    wr_bitmap_u = bl_plat->mp_regs[bl_device->reg->wr_bitmap_u];
	    bitmap = wr_bitmap_l;
	    bitmap |= wr_bitmap_u << 8;
		if(bitmap&0xfffe){
			down_cnt ++;			
		BL_DBG("D %d, bitmap=0x%x, mp_wb=0x%x, cur_wp=%d\n", down_cnt, bitmap, bl_plat->mp_wr_bitmap, bl_hw->plat->curr_wr_port);
	    sdio_ireg |=DN_LD_HOST_INT_STATUS;
        }
    }

	if(sdio_ireg) {
		BL_DBG(">>>bl_get_interrupt_status, sdio_ireg=0x%x\n", sdio_ireg);
		spin_lock_irq(&bl_hw->int_lock);
		bl_hw->int_status |= sdio_ireg;
		spin_unlock_irq(&bl_hw->int_lock);
	}
}

/**
 * bl_irq_hdlr - IRQ handler
 *
 * Handler registerd by the platform driver
 */
void bl_irq_hdlr(struct sdio_func *func)
{
	struct bl_hw *bl_hw;
	
	BL_DBG(BL_FN_ENTRY_STR);
	
    bl_hw = sdio_get_drvdata(func);
	if(!bl_hw) {
		printk("get bl_hw failed!\n");
		return;
	}

	bl_get_interrupt_status(bl_hw, 0);

	/* track int count */
    bl_hw->int_cnt ++;
#if 0
	/*irq/irq top handler call spin_lock, no need to disable irq
	 *but, workqueue and user thread need disable irq
	 */
	spin_lock_irq(&bl_hw->main_proc_lock);
	if (bl_hw->bl_processing) {
		BL_DBG("bl_irq_hdlr: set more_task_flag true\n");
		bl_hw->more_task_flag = true;
		spin_unlock_irq(&bl_hw->main_proc_lock);
	} else {
		spin_unlock_irq(&bl_hw->main_proc_lock);
		BL_DBG("bl_irq_hdlr: schedule work\n");
		queue_work(bl_hw->workqueue, &bl_hw->main_work);
	}
	//bl_queue_main_work(bl_hw);
#endif

	bl_main_process(bl_hw);
}

static int bl_pending_msg_hdl(struct bl_hw *bl_hw)
{    
	struct bl_cmd *cur = NULL;
	int ret = 0;
	u32 count = 0, rd_reg_cnt = 0;
	u8 wr_bitmap_l = 0;
	struct bl_cmd *hostid = (struct bl_cmd *)(bl_hw->ipc_env->msga2e_hostid);

	struct bl_device *device = (struct bl_device *)bl_hw->plat->priv;

	list_for_each_entry(cur, &bl_hw->cmd_mgr.cmds, list) {
			//printk("cur:0x%x, cur->tkn=%d, hostid->tkn=%d, queue_sz=%d, max_queue_sz=%d\n", 
			//        cur, cur->tkn, hostid->tkn, bl_hw->cmd_mgr.queue_sz, bl_hw->cmd_mgr.max_queue_sz);
			if(cur->tkn == hostid->tkn) {
				//bl_read_reg(bl_hw, device->reg->wr_bitmap_l, &wr_bitmap_l);
				wr_bitmap_l = bl_hw->plat->mp_regs[device->reg->wr_bitmap_l];
				while((!(wr_bitmap_l & CTRL_PORT_MASK)) && count < 200){
					msleep(5);
					count++;
					ret = bl_read_reg(bl_hw, device->reg->wr_bitmap_l, &wr_bitmap_l);
					if (ret) {
						do {
							ret = bl_read_reg(bl_hw, device->reg->wr_bitmap_l, &wr_bitmap_l);
							rd_reg_cnt ++;
						} while (ret && rd_reg_cnt < BL_SDIO_RD_WR_RETRY);
					}
					BL_DBG("wait for cmd port ready!\n");
				};

				if(count >=200){
					BL_DBG("wait for cmd port timeout!\n");
					kfree(cur->a2e_msg);
					return -1;
				}
				if(cur->flags |BL_CMD_FLAG_WAIT_PUSH)
					cur->flags &= ~BL_CMD_FLAG_WAIT_PUSH;

				if (bl_mod_params.mp_mode) {
					// bl_write_data_sync(bl_hw, (u8 *)(cur->a2e_msg->param), ((cur->a2e_msg->param_len + 3)/4) *4, bl_hw->plat->io_port + CTRL_PORT);
					bl_write_data_sync(bl_hw, (u8 *)(cur->a2e_msg->param), 2048, 0x10000);
				} else {
				    //printk("write sdio cur:0x%x, len:%d\r\n", cur, cur->a2e_msg->param_len);
					bl_write_data_sync(bl_hw, (u8 *)(cur->a2e_msg), ((sizeof(struct lmac_msg) + cur->a2e_msg->param_len + 3)/4) *4, bl_hw->plat->io_port + CTRL_PORT);
		        }
				kfree(cur->a2e_msg);
				break;
			} 
	}

	/*we are in bootom handler, so we just use spin_lock*/
	spin_lock(&bl_hw->cmd_lock);
	bl_hw->cmd_sent = false;
	spin_unlock(&bl_hw->cmd_lock);

	return 0;

}

void main_wq_hdlr(struct work_struct *work)
{
	struct bl_hw *bl_hw = container_of(work, struct bl_hw, main_work);

	BL_DBG(BL_FN_ENTRY_STR);
	
	bl_main_process(bl_hw);
}

static int bl_upload_hdl(struct bl_hw *bl_hw)
{
	u8 rd_bitmap_l = 0;
	u8 rd_bitmap_u = 0;
	u32 len_reg_l = 0;
	u32 len_reg_u = 0;
	u32 bitmap;
	u32 rx_len;
	u32 port = CTRL_PORT;
	int ret = 0;
	struct sk_buff *skb;
	u8 cr;
	u32 pind;
	u8 *curr_ptr;
	u32 pkt_len;
	u32 pkt_type;
	u32 hw_idx;
	u32 reserved;
    u32 cmd53_port = 0;
	int avail_port_num = 0;
    u32 rd_bitmap = 0;
    bool rd_twice = false;
#ifdef TEST_REBOOT_TIME
    //struct timespec ts;    
    struct rtc_time tm;
    long start;
    long end;
#endif
    static u16_l last_msg_cnt = 0xFFFF;
    static u16_l last_data_cnt = 0xFFFF;

	struct bl_plat *bl_plat = bl_hw->plat;
	struct bl_device *bl_device = (struct bl_device *)bl_plat->priv;
	int cnt = 0;

	BL_DBG(BL_FN_ENTRY_STR);

//#define TEST_REBOOT_TIME

#ifdef TEST_REBOOT_TIME
    getnstimeofday(&ts);
    rtc_time_to_tm(ts.tv_sec,&tm);
    start = ts.tv_nsec/1000;
    //printk("[ST]:%d-%d-%d %d:%d:%d %ld\n", tm.tm_year+1900,tm.tm_mon, tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec, start);
#endif
    // reset cmd state to inited when there is anything from FW
   // if (bl_hw->cmd_mgr.state == BL_CMD_MGR_STATE_CRASHED) 
    //    bl_hw->cmd_mgr.state = BL_CMD_MGR_STATE_INITED;

    bl_hw->mpa_rx_data.pkt_cnt = 0;

	rd_bitmap_l = bl_plat->mp_regs[bl_device->reg->rd_bitmap_l];
	rd_bitmap_u = bl_plat->mp_regs[bl_device->reg->rd_bitmap_u];

	bitmap = rd_bitmap_l;
	bitmap |= rd_bitmap_u << 8;

	bl_plat->mp_rd_bitmap = bitmap;
    rd_bitmap = bitmap;

	BL_DBG("int: UPLD: rd_bitmap=0x%x, lost_int_flag=%d\n", bl_plat->mp_rd_bitmap, bl_hw->lost_int_flag);
	
	rd_bitmap &= 0xFFFFFFFE;
	while(rd_bitmap) {
		rd_bitmap = rd_bitmap & (rd_bitmap-1);
		avail_port_num++;
	}
    
    //printk("rd_bitmap=0x%x, available rd port nums=%d,------------------------------- ", bl_plat->mp_rd_bitmap, avail_port_num);
    if(avail_port_num > 7) {
        //printk("will read twice\n");
        rd_twice = true;
    } else {
        //printk("\n");
    }

TWICE:

	while(true) {
		ret = bl_get_rd_port(bl_hw, &port);
		if (ret) {
			//printk("info: no more rd_port available\n");
			break;
		}

		len_reg_l = bl_plat->mp_regs[bl_device->reg->rd_len_p0_l + (port << 1)];
		len_reg_u = bl_plat->mp_regs[bl_device->reg->rd_len_p0_u + (port << 1)];
		rx_len = (len_reg_u << 8) + len_reg_l;

		//printk("rd_port: port=0x%x, rx_len=%d\n", port, rx_len);

		if (rx_len < sizeof(struct sdio_hdr) || rx_len > 4096) {
			printk("Invaild rx_len\n");
			return -1;
		}
		
		rx_len = ((rx_len + BL_SDIO_BLOCK_SIZE - 1)/BL_SDIO_BLOCK_SIZE) * BL_SDIO_BLOCK_SIZE;


		BL_DBG("after round: rx_len=%d, port=%d\n", rx_len, port);

        if(port == CTRL_PORT) {
			skb = dev_alloc_skb(rx_len);
			if (!skb) {
				printk("%s: failed to alloc skb,use reserved skb\n", __func__);
				skb = bl_hw->msg_skb;
			    if (skb) {
			        skb->data = skb->tail = skb->head;
			        skb->len = 0;
				} else {
                    return -1;
				}			

			}

            ret = bl_read_data_sync(bl_hw, skb->data, rx_len, bl_hw->plat->io_port + port);
            if(ret) {
                do {
                    ret = bl_read_data_sync(bl_hw, skb->data, rx_len, bl_hw->plat->io_port + port);
                    cnt ++;
                } while (ret && cnt < BL_SDIO_RD_WR_RETRY);

                if(cnt >= BL_SDIO_RD_WR_RETRY) {
                    printk("bl_upload_hdl port[%d] read 10 times\n", port);
                    dev_kfree_skb_any(skb);
                    return -1;
                }
            }
			pkt_len  = le16_to_cpu(*(__le16 *) (skb->data + 0));
			pkt_type = le16_to_cpu(*(__le16 *) (skb->data + 2));
			hw_idx	 = le16_to_cpu(*(__le16 *) (skb->data + 4));
			reserved = le16_to_cpu(*(__le16 *) (skb->data + 6));
			if (last_msg_cnt == (u16_l)reserved){
				BL_DBG("duplicate msg, drop it last msg cnt %d current cnt %d\n",last_msg_cnt,reserved);
				if (skb != bl_hw->msg_skb )
			        dev_kfree_skb_any(skb);
			    continue;
			}			
			skb_put(skb, rx_len);
			if(bl_decode_rx_packet(bl_hw, skb, pkt_type, hw_idx, port)) {
				printk("bl_rx_process error\n");
				goto term_cmd;
			}
			last_msg_cnt = (u16_l)reserved;

            #ifdef TEST_REBOOT_TIME
                getnstimeofday(&ts);
                rtc_time_to_tm(ts.tv_sec,&tm);
                end = ts.tv_nsec/1000;
                if(end-start >10000)
                    printk("Rx Msg over 10ms: start=%ld, end=%ld\n", start, end);
                //printk("[E0]:%d-%d-%d %d:%d:%d %0.4d\n",tm.tm_year+1900,tm.tm_mon, tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec, txc.time.tv_usec);
            #endif

            continue;
        }else {
            skb = dev_alloc_skb(rx_len);
            if (!skb) {
                printk("%s: failed to alloc skb \n", __func__);
                skb = NULL;
            } 
		}

        //printk("start to record multi port: port=0x%x\n", port);

        bl_hw->mpa_rx_data.buf_len += rx_len;
        if(!bl_hw->mpa_rx_data.pkt_cnt)
            bl_hw->mpa_rx_data.start_port = port;

        if(bl_hw->mpa_rx_data.start_port <= port) {
            bl_hw->mpa_rx_data.ports |= (1 << bl_hw->mpa_rx_data.pkt_cnt);
        } else {
            bl_hw->mpa_rx_data.ports |= (1 << (bl_hw->mpa_rx_data.pkt_cnt + 1));
        }

        bl_hw->mpa_rx_data.buf_arr[bl_hw->mpa_rx_data.pkt_cnt] = skb; 
        bl_hw->mpa_rx_data.len_arr[bl_hw->mpa_rx_data.pkt_cnt] = rx_len; 
        bl_hw->mpa_rx_data.mp_rd_port[bl_hw->mpa_rx_data.pkt_cnt] = port; 
        bl_hw->mpa_rx_data.pkt_cnt++;
        if(bl_hw->mpa_rx_data.pkt_cnt==7 && rd_twice == true) {
            //printk("pkt_cnts > 7, other port will read next time, ports=0x%x, cnt=%d\n", bl_hw->mpa_rx_data.ports, bl_hw->mpa_rx_data.pkt_cnt);
            break;
        }
    }

    if(bl_hw->mpa_rx_data.pkt_cnt != 0) {

        //printk("ports=0x%x, pkt_cnt=%d\n", bl_hw->mpa_rx_data.ports, bl_hw->mpa_rx_data.pkt_cnt);
        if (bl_hw->mpa_rx_data.buf)
            memset(bl_hw->mpa_rx_data.buf, 0, BL_RX_DATA_BUF_SIZE_16K);

	        /*recv packet*/
	    cmd53_port = (bl_hw->plat->io_port | BL_SDIO_MPA_ADDR_BASE |
	    				(bl_hw->mpa_rx_data.ports << 4)) + bl_hw->mpa_rx_data.start_port;

        //printk("mpa_buf_len=%d, cmd53_port=0x%08x, mpa_rx_data.buf=%p\n", bl_hw->mpa_rx_data.buf_len, cmd53_port, bl_hw->mpa_rx_data.buf);
	    
	    ret = bl_read_data_sync(bl_hw, bl_hw->mpa_rx_data.buf, bl_hw->mpa_rx_data.buf_len, cmd53_port);
	    if(ret){
	        cnt = 0;
	        do {
	            ret = bl_read_data_sync(bl_hw, bl_hw->mpa_rx_data.buf, bl_hw->mpa_rx_data.buf_len, cmd53_port);
	            cnt ++;
	        } while (ret && cnt < BL_SDIO_RD_WR_RETRY);

	        if(cnt >= BL_SDIO_RD_WR_RETRY) {
	            printk("bl_read_data_sync failed, ret=%d\n", ret);
	        }
	    }
        curr_ptr = bl_hw->mpa_rx_data.buf;

        //printk("mpa_rx_data.buf: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x \n",
        //                        curr_ptr[0],
        //                        curr_ptr[1],
        //                        curr_ptr[2],
        //                        curr_ptr[3],
        //                        curr_ptr[4],
        //                        curr_ptr[5],
        //                        curr_ptr[6],
        //                        curr_ptr[7]
        //        );
	    	
        for(pind = 0; pind < bl_hw->mpa_rx_data.pkt_cnt; pind++) {
            skb = (struct sk_buff *)(bl_hw->mpa_rx_data.buf_arr[pind]);
			if (!skb) {
                printk("skb may allocate fail, drop this rx packet \n");
				continue;
			}				
            memcpy((u8 *)((struct sk_buff *)(bl_hw->mpa_rx_data.buf_arr[pind])->data), curr_ptr, bl_hw->mpa_rx_data.len_arr[pind]);

        	pkt_len  = le16_to_cpu(*(__le16 *) (((bl_hw->mpa_rx_data.buf_arr[pind])->data + 0)));
        	pkt_type = le16_to_cpu(*(__le16 *) (((bl_hw->mpa_rx_data.buf_arr[pind])->data + 2)));
        	hw_idx	 = le16_to_cpu(*(__le16 *) (((bl_hw->mpa_rx_data.buf_arr[pind])->data + 4)));
        	reserved = le16_to_cpu(*(__le16 *) (((bl_hw->mpa_rx_data.buf_arr[pind])->data + 6)));
        
        	BL_DBG("pkt_len=%d, pkt_type=%d, hw_idx=%d, reserved=%d\n", pkt_len, pkt_type, hw_idx, reserved);

			bl_hw->msg_idx = (u16)((reserved & 0xFFF0) >> 4);
			if(bl_hw->msg_idx == last_data_cnt || (reserved & 0xF) > 4) {
				BL_DBG("**drop dup data cnt=%d-%d, pad=%d, mp_rd_bm=%x, curr_rd_port=%d\n",bl_hw->msg_idx, last_data_cnt, (u8_l)(reserved & 0xF), bl_hw->plat->mp_rd_bitmap, bl_hw->plat->curr_rd_port);
				dev_kfree_skb_any(skb);
				continue;
			}
			last_data_cnt = bl_hw->msg_idx;

			if(!skb->len) {
				skb_put(bl_hw->mpa_rx_data.buf_arr[pind], min((pkt_len + sizeof(struct sdio_hdr) + (u8_l)(reserved & 0xF)),bl_hw->mpa_rx_data.len_arr[pind]));
				if(bl_decode_rx_packet(bl_hw, (struct sk_buff *)(bl_hw->mpa_rx_data.buf_arr[pind]), pkt_type, hw_idx, bl_hw->mpa_rx_data.mp_rd_port[pind])) {
					printk("bl_rx_process error\n");
					goto term_cmd;
				}
			} else {
				printk("wrong skb skb head %px skb data %px skb len %x put len %x, tail %#lx end %#lx\n", skb->head, skb->data, skb->len, bl_hw->mpa_rx_data.len_arr[pind], (unsigned long)skb->tail, (unsigned long)skb->end);
				printk("pind %x bl_hw->mpa_rx_data.pkt_cnt %x avail_port_num %x \n", pind, bl_hw->mpa_rx_data.pkt_cnt, avail_port_num);
			}
        
        	curr_ptr += bl_hw->mpa_rx_data.len_arr[pind];
        }
        
        //printk("clear pkt_cnt and ports, buf_len\n");
        bl_hw->mpa_rx_data.pkt_cnt = 0;
        bl_hw->mpa_rx_data.ports = 0;
        bl_hw->mpa_rx_data.buf_len = 0;
        #ifdef TEST_REBOOT_TIME
            getnstimeofday(&ts);
            rtc_time_to_tm(ts.tv_sec,&tm);
            end = ts.tv_nsec/1000;
            if(end-start >10000)
                printk("Rx Data over 10ms: start=%ld, end=%ld\n", start, end);
            //printk("[ED]:%d-%d-%d %d:%d:%d %0.4d\n",tm.tm_year+1900,tm.tm_mon, tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec, txc.time.tv_usec);
        #endif
    }

    if(rd_twice == true) {
        //printk("start read twice\n");
        rd_twice = false;
        goto TWICE;
    }


	return 0;

	term_cmd:
	/* terminate cmd */
	if (bl_read_reg(bl_hw, CONFIGURATION_REG, &cr))
		printk("read CFG reg failed\n");
	else
		printk("info: CFG reg val = %d\n", cr);

	if (bl_write_reg(bl_hw, CONFIGURATION_REG, (cr | 0x04)))
		printk("write CFG reg failed\n");
	else
		printk("info: write success\n");

	if (bl_read_reg(bl_hw, CONFIGURATION_REG, &cr))
		printk("read CFG reg failed\n");
	else
		printk("info: CFG reg val =%x\n", cr);

	return -1;
}

static int bl_download_hdl(struct bl_hw *bl_hw)
{
	u8 wr_bitmap_l = 0;
	u8 wr_bitmap_u = 0;
	u32 bitmap;

	struct bl_plat *bl_plat = bl_hw->plat;
	struct bl_device *bl_device = (struct bl_device *)bl_plat->priv;

	BL_DBG(BL_FN_ENTRY_STR);

	//bl_read_reg(bl_hw, bl_device->reg->wr_bitmap_l, &wr_bitmap_l);
	//bl_read_reg(bl_hw, bl_device->reg->wr_bitmap_u, &wr_bitmap_u);
	wr_bitmap_l = bl_plat->mp_regs[bl_device->reg->wr_bitmap_l];
	wr_bitmap_u = bl_plat->mp_regs[bl_device->reg->wr_bitmap_u];
	bitmap = wr_bitmap_l;
	bitmap |= wr_bitmap_u << 8;
	
	bl_plat->mp_wr_bitmap = bitmap;

	BL_DBG( "int: DNLD: wr_bitmap=0x%x, curr_wr_port=0x%x\n", bl_plat->mp_wr_bitmap, bl_plat->curr_wr_port);

	return 0;
}

#ifdef CONFIG_BL_MP
static int bl_upload_mp_hdl(struct bl_hw *bl_hw)
{
	u8 rd_bitmap_l = 0;
	u8 rd_bitmap_u = 0;
	u32 bitmap;
	u32 rx_len = 2048;
	int ret = 0;
	struct sk_buff *skb = NULL;
	u16 pkt_len;
	u16 msg_id;

	struct bl_plat *bl_plat = bl_hw->plat;
	struct bl_device *bl_device = (struct bl_device *)bl_plat->priv;

	BL_DBG(BL_FN_ENTRY_STR);

	bl_read_reg(bl_hw, bl_device->reg->rd_bitmap_l, &rd_bitmap_l);
	bl_read_reg(bl_hw, bl_device->reg->rd_bitmap_u, &rd_bitmap_u);
	
	bitmap = rd_bitmap_l;
	bitmap |= rd_bitmap_u << 8;

	bl_plat->mp_rd_bitmap = bitmap;

	BL_DBG("int: UPLD: rd_bitmap=0x%x, lost_int_flag=%d\n", bl_plat->mp_rd_bitmap, bl_hw->lost_int_flag);

	skb = dev_alloc_skb(rx_len);
	if (!skb) {
		printk("%s: failed to alloc skb", __func__);
		return -ENOMEM;
	}

	ret = bl_read_data_sync(bl_hw, skb->data, rx_len, 0x10000);
	if(ret) {
		printk("%s: read_data_sync fail, ret:%d\n", __func__, ret);
		dev_kfree_skb_any(skb);
		return -1;
	}
	skb_put(skb, rx_len);

	pkt_len  = le16_to_cpu(*(__le16 *) (skb->data + 0));

	skb_push(skb, 8);
	msg_id = MP_TEST_CFM;
	(*(__le16 *)(skb->data + 0)) = cpu_to_le16(msg_id);
	(*(__le16 *)(skb->data + 6)) = cpu_to_le16(pkt_len+2);
	bl_hw->ipc_env->cb.recv_msg_ind(bl_hw, (void *)(skb->data));
	dev_kfree_skb_any(skb);

	return 0;
}
#endif

int bl_main_process(struct bl_hw *bl_hw)
{
	u8 sdio_ireg;
	int ret = 0;
 
	BL_DBG(BL_FN_ENTRY_STR);

	/*we know irq is enable, so here use spin_lock_irq*/
	spin_lock_irq(&bl_hw->main_proc_lock);
	/* Check if already processing */
	if (bl_hw->bl_processing) {
		BL_DBG("bl_main_process: more_task_flag = true\n");
		bl_hw->more_task_flag = true;
		spin_unlock_irq(&bl_hw->main_proc_lock);
		goto exit_main_proc;
	} else {
		bl_hw->bl_processing = true;
		spin_unlock_irq(&bl_hw->main_proc_lock);
	}

	bl_hw->lost_int_flag = 0;

process_start:
    if(bl_hw->surprise_removed)
        return ret;

	spin_lock_irq(&bl_hw->int_lock);
	sdio_ireg = bl_hw->int_status;
	bl_hw->int_status = 0;
	spin_unlock_irq(&bl_hw->int_lock);

	
	/* 1.handle the msg */
	if (!list_empty(&bl_hw->cmd_mgr.cmds)) {
		if(bl_hw->cmd_sent) {
			ret = bl_pending_msg_hdl(bl_hw);
			if(ret) {
				printk("send msg failed, ret=%d\n", ret);
				return ret;
			}
		}
	}

	/* 2.handle recv data */
	if (sdio_ireg & UP_LD_HOST_INT_STATUS) {
#ifdef CONFIG_BL_MP
		if (bl_mod_params.mp_mode) 
			ret = bl_upload_mp_hdl(bl_hw);
		else
#endif
			ret = bl_upload_hdl(bl_hw);
		if(ret || (bl_hw->resend))
			goto exit_main_proc;			
	}

	/* 3. if wr port ready, see if data need to transmit */
	if ((sdio_ireg & DN_LD_HOST_INT_STATUS) && !(bl_hw->plat->mp_wr_bitmap & 0xFFFE)){
		ret = bl_download_hdl(bl_hw);
	}

	/* 4. handle tx data */
	BL_DBG("bl_hw->recovery_flag---> %s\n", bl_hw->recovery_flag ? "true":"false");
	if(!bl_hw->recovery_flag) {
		BL_DBG("bl_main_process: start call bl_hwq_process_all\n");
		bl_hwq_process_all(bl_hw);
		BL_DBG("bl_main_process: end  call bl_hwq_process_all\n");
	}
	
	spin_lock_irq(&bl_hw->main_proc_lock);
	if (bl_hw->more_task_flag) {
		bl_hw->more_task_flag = false;
		spin_unlock_irq(&bl_hw->main_proc_lock);
		bl_hw->lost_int_flag = 1;
		BL_DBG("go to process start\n");
		goto process_start;
	}
	bl_hw->bl_processing = false;
	spin_unlock_irq(&bl_hw->main_proc_lock);

exit_main_proc:
	return ret;
}
