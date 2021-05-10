/* GStreamer Round Robin
 * Copyright (C) 2019 Net Insight AB
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-roundrobin
 * @title: roundrobin
 *
 * This is a generic element that will distribute equally incoming
 * buffers over multiple src pads. This is the opposite of tee
 * element, which duplicates buffers over all pads. This element 
 * can be used to distrute load across multiple branches when the buffer
 * can be processed independently.
 */

#include "gstroundrobin.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <stdint.h>

GST_DEBUG_CATEGORY_STATIC (gst_round_robin_debug);
#define GST_CAT_DEFAULT gst_round_robin_debug

/* Round Robin Properties */
enum {
  PROP_0,
  PROP_NUM_SRC_PADS,
  PROP_RATE,
  PROP_REPETITION,
  PROP_PYTHON_RATE,
  PROP_PYTHON_REPETITION
};

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src_%d",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("ANY"));


struct _GChannel
{
  gfloat rate;    // weight of channel
  gint index;    // index of channel 
  gint pkt_count;    // number of packets that must pass in a round
  gint seqnum;   // next seqnum to assign
  gint jump;   // number of packets that may not pass
};

typedef struct _GChannel gchannel;

struct _GstRoundRobin
{
  GstElement parent;    // the element 
  gint nsrcpads;    // number of channels
  gfloat *repetition;   // array of coding rate
  gint *pkt_rep;     // number of packet to replicate
  gchannel *channel;   // array of channels
};

G_DEFINE_TYPE_WITH_CODE (GstRoundRobin, gst_round_robin,
    GST_TYPE_ELEMENT, GST_DEBUG_CATEGORY_INIT (gst_round_robin_debug,
        "roundrobin", 0, "Round Robin"));

static gboolean update = TRUE;    // flag to control if weights and rates are valid
static gboolean def = TRUE;    // flag to understand if there are weights
static gint rround = 100;      // number of packets in a round
static guint32 ssrc = 3090172512;      
static gint count;          // number of packet that is passing
static gint in;
static gint num1;
static gint count_rate = 0;    // used in python set_property
static gint count_repetition = 0;  // used in python set_property
static gfloat *store_rate;   // used in python set_property
static gfloat *store_repetition;    // used in python set_property

static gint
cmpfunc1 (const void * a, const void * b) {
  if (((gchannel*)a)->rate > ((gchannel*)b)->rate)
    return 1;
  else if (((gchannel*)a)->rate < ((gchannel*)b)->rate)
    return -1;
  else
    return 0;
}

static gint 
cmpfunc2 (const void * a, const void * b) {
  return ( ((gchannel*)a)->index - ((gchannel*)b)->index );
}

static GstFlowReturn
gst_round_robin_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstRoundRobin *disp = (GstRoundRobin *) parent;
  GstElement *elem = (GstElement *) parent;
  GstPad *src_pad = NULL;
  GstFlowReturn ret;

  GList *pads = GST_ELEMENT_CAST (disp)->srcpads; 
  gint nsrcpads = elem->numsrcpads;
  gfloat sum = 0.00, sum1 = 0.00, sum2 = 0.00, decr = 0.00, min=0.00, max=0.00;
  gint dup = 1, st = 0 , ptot = 0, j = 0, m = 0, c_jump1 = 0, c_jump2 = 0, done = 0, num = 0; 
  gboolean flag = FALSE, constrains = FALSE;
  guint32 data[2];      // array for header extension
  gint *choose = g_new0(gint, nsrcpads);   // array to understand what channel is chosen
  gint *a_jump = g_new0 (gint, nsrcpads);   // array to understand what channel must be chosen
  
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  // case where there are no srcpads
  if (G_UNLIKELY (!pads)){
    ret = GST_FLOW_NOT_LINKED;
    return ret;
  }

  // case where no properties have been specified
  if(def == TRUE){
    GST_LOG_OBJECT (disp, "Setting default values...");
   
    // allocate arrays of probabilities
    disp->channel = g_new (gchannel, nsrcpads);
    disp->repetition = g_new (gfloat, nsrcpads);

    for(gint i = 0; i < nsrcpads; i++) {
      disp->channel[i].index = i;
      disp->channel[i].rate = 1 / (float) nsrcpads;     // fairness 
      if(i == 0)
        disp->repetition[i] = 1.0;   // not replicate
      else
        disp->repetition[i] = 0.0;
      sum += disp->channel[i].rate;
    } 
    if(sum != 1.0)
      disp->channel[0].rate += 0.01;

    def = FALSE;   //flag to skip this algorithm at the next packet
  }

  GST_OBJECT_LOCK (disp);

  // if the probabilities have been changed
  if(update == FALSE && (count == 0 || count == rround)) { 

    // if the declared number of pads is not correct, error
     if(disp->nsrcpads != nsrcpads) {
      ret = GST_FLOW_ERROR;
      GST_LOG_OBJECT (disp, "Error channels number!");
      GST_OBJECT_UNLOCK (disp);
      return ret;
    }

    // setting values with sender in python
    if(store_rate) {
      if (!disp->channel)
        disp->channel = g_new (gchannel, nsrcpads);    
      
      for(gint i = 0; i < nsrcpads; i++){
        disp->channel[i].rate = store_rate[i];
        disp->channel[i].index = i; 
      }
    }

    if(store_repetition) {
      if (!disp->repetition)
        disp->repetition = g_new (gfloat, nsrcpads);

      for(gint i = 0; i < nsrcpads; i++)
        disp->repetition[i] = store_repetition[i];    
    }


    // calculate the sum of the probabilities
    for(gint i = 0; i < nsrcpads; i++) {
      if(disp->channel[i].rate == 1.0){
        st=i;
        flag=TRUE;
      }
      sum1 += disp->channel[i].rate;
      sum2 += disp->repetition[i];
    }

    // if the sums are not equal to 1, error
    if(sum1 != 1.0 || sum2 != 1.0) {
      ret = GST_FLOW_ERROR;
      GST_LOG_OBJECT (disp, "Error sum values!");
      GST_OBJECT_UNLOCK (disp);
      return ret;
    }
    
    // a channel cannot have weight equal to 1 
    if(flag == TRUE){
      for(gint i = 0; i < nsrcpads; i++){
        if(i != st){
          decr += 0.01;
          disp->channel[i].rate = 0.01;
        }
        else
          continue;
      }   
      disp->channel[st].rate = 1.00 - decr; 
      flag = FALSE;
    }
    
    update = TRUE;  // flag to save that all is correct
  }

  // initial seqnum 
  if(count == 0)
    for(gint i = 0; i< nsrcpads; i++)
      disp->channel[i].seqnum = 10000;   


  // new start of the round
  if(count == rround || count == 0){
    GST_LOG_OBJECT (disp, "Starting new round...");
    count = 0;
    in = nsrcpads-1;   // index for replication

    if(disp->pkt_rep)
      g_free(disp->pkt_rep);

    disp->pkt_rep = g_new (gint, nsrcpads);

    // calculate the number of total packets in this round
    for(gint i = 0; i < nsrcpads; i++) {
      choose[i] = 0;
      gint reps = (int)((disp->repetition[i])*100);
      disp->pkt_rep[i] = reps;
      ptot = ptot + reps*(i+1);
    }

    qsort(disp->channel, nsrcpads, sizeof(gchannel), cmpfunc1);

    // constrains
    for(gint i = nsrcpads-1;  i >= 0; i--) {
      m = disp->channel[j].pkt_count = ceil((disp->channel[j].rate)*ptot); 
      disp->channel[j].jump = 100 - m;
      min = round ((int)((disp->repetition[nsrcpads-1])*10000) / ptot) / 100;  // minimum constrain for weight
      max = round(10000 / ptot) / 100 + 0.01;    // maximum constrain for weight
      // are respected?
      if (disp->channel[j].rate < min || disp->channel[j].rate > max ) {
        constrains = TRUE;
        GST_LOG_OBJECT (disp, "Constrains not respected!");
        break;
      }
      j++;
    }  

    num1 = disp->channel[nsrcpads-1].index; // first channel to choose 

    qsort(disp->channel, nsrcpads, sizeof(gchannel), cmpfunc2);

    // if constrains are not respected
    if(constrains == TRUE){
      for(gint k = 0;  k < nsrcpads; k++) {
          disp->channel[k].rate = 1 / (float) nsrcpads;
          sum += disp->channel[k].rate;
        }
      if(sum != 1.0)
        disp->channel[0].rate += 0.01;
    }
  }

  count++;  // new packet

  // to know how much to replicate
  restart:
    if(in < 0)
      in = nsrcpads-1;

    if(disp->pkt_rep[in] > 0) {
      disp->pkt_rep[in] -= 1;
      dup = in+1;
      in--;
    }
    else {
      in--;
      goto restart;
    }
   
  // control if there are channel that must be chosen
  j=0;
  for(gint i = 0; i < nsrcpads; i++){
    if (num1 >= nsrcpads)
          num1 = 0;
    if(disp->channel[num1].jump <= 0 && disp->channel[num1].pkt_count > 0){
      a_jump[j] = num1;
      j++;
      c_jump1++;
      c_jump2++;
    }
    num1++;
  }
 
  //scheduler 
  j=0;
  while(done < dup) {
      
    if(dup - done > c_jump1){

     // choose the next channel 
      if (num1 >= nsrcpads)
        num1 = 0;
      if(disp->channel[num1].pkt_count <= 0) {
        num1++;
        continue;
      }
      else {
        num = num1;
        num1++;
      }
    }

    else {
      // choose the obligatory channel
      num = a_jump[j];
      j++;
    }

    choose[num] = 1;     // signal what channel is chosen
    done++;
    // update data of channel
    disp->channel[num].pkt_count -= 1;
    disp->channel[num].seqnum += 1; 
  
    for(gint i=0; i < c_jump2; i++)
      if(num == a_jump[i])
        c_jump1--;
      
    //header extension
    buffer = gst_buffer_make_writable(buffer);
    gst_rtp_buffer_map (buffer, GST_MAP_READWRITE, &rtp);
    gst_rtp_buffer_set_extension(&rtp, TRUE);
    if(done == 1) {
      data[0] = gst_rtp_buffer_get_ssrc (&rtp);    // save SSRC
      data[1] = gst_rtp_buffer_get_seq (&rtp);     // save seqnum
    }
    gst_rtp_buffer_add_extension_onebyte_header(&rtp, 5, data, sizeof(data));
    gst_rtp_buffer_set_ssrc (&rtp,(guint32) ssrc+num);      // change SSRC
    gst_rtp_buffer_set_seq (&rtp, (guint16) (disp->channel[num].seqnum));     //change seqnum
        
    src_pad = g_list_nth_data (pads, num);    // choose the correct src-pad
          
    if (src_pad) 
      gst_object_ref (src_pad);
    else 
      continue;

    // bring the packet to the correct pad
    ret = gst_pad_push (src_pad, gst_buffer_ref(buffer));  
    gst_object_unref (src_pad);

    if(ret != GST_FLOW_OK)
      break;
    
  }

  // update data of channel
  for(gint i=0; i < nsrcpads; i++)
    if(choose[i] == 0)
      disp->channel[i].jump -= 1;

  GST_OBJECT_UNLOCK (disp);
  g_free(a_jump);
  g_free(choose);

  return ret;
}

static GstPad *
gst_round_robin_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstPad *pad;

  pad = gst_element_get_static_pad (element, name);
  if (pad) {
    gst_object_unref (pad);
    return NULL;
  }

  pad = gst_pad_new_from_static_template (&src_templ, name);
  gst_element_add_pad (element, pad);

  return pad;
}

static void
gst_round_robin_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstRoundRobin *disp = GST_ROUND_ROBIN (object);
  gint nsrcpads;
  gfloat prob;
  const GValue *item;

  GST_OBJECT_LOCK (disp);

  switch (prop_id) {
    case PROP_NUM_SRC_PADS:
      disp->nsrcpads = g_value_get_int (value);       //store the number of srcpads
      break;
    case PROP_RATE:{
      nsrcpads = disp->nsrcpads;
      
      if (!disp->channel)
        disp->channel = g_new (gchannel, nsrcpads);
      
      g_return_if_fail (gst_value_array_get_size (value) == nsrcpads);

      // store the vector of probability rate
      for(gint index = 0; index < nsrcpads; index++) {
        item = gst_value_array_get_value (value, index);
        g_return_if_fail (G_VALUE_HOLDS_FLOAT (item));
        prob = g_value_get_float (item);
        disp->channel[index].rate = prob;
        disp->channel[index].index = index;
      }
      update = FALSE;  // warn the chain function to check the values
      def = FALSE;  // warn the chain functiin that must not use the default values
      break;
    }
    case PROP_REPETITION:{
      nsrcpads = disp->nsrcpads;

      if (!disp->repetition)
        disp->repetition = g_new (gfloat, nsrcpads);

      g_return_if_fail (gst_value_array_get_size (value) == nsrcpads);

      // store the vector of probability repetition
      for(gint index = 0; index < nsrcpads; index++) {
        item = gst_value_array_get_value (value, index);
        g_return_if_fail (G_VALUE_HOLDS_FLOAT (item));
        prob = g_value_get_float (item);
        disp->repetition[index] = prob;
      }
      update = FALSE;   // warn the chain function to check the values
      def = FALSE;   // warn the chain function that must not use the default values
      break;
    }
    case PROP_PYTHON_RATE:{   // property used for pipeline in python
      nsrcpads = disp->nsrcpads;

      if(!store_rate)
        store_rate = g_new (gfloat, nsrcpads);

      // store the value of probability rate
      store_rate[count_rate] = g_value_get_float (value);
      count_rate++; 

      // when the function reads all the values, it updates
      if(count_rate == nsrcpads) {
        update = FALSE;   // warn the chain function to check the values
        def = FALSE;   // warn the chain function that must not use the default values
        count_rate = 0;    // reset the counter
      }   
      break;  
    }
    case PROP_PYTHON_REPETITION:{
      nsrcpads = disp->nsrcpads;

      if(!store_repetition)
        store_repetition = g_new (gfloat, nsrcpads);
      
      // store the value of probability repetition
      store_repetition[count_repetition] = g_value_get_float (value);
      count_repetition++; 

      // when the function reads all the values, it updates
      if(count_repetition == nsrcpads) {
        update = FALSE;  // warn the chain function to check the values
        def = FALSE;   // warn the chain function that must not use the default values
        count_repetition = 0;    // reset the counter
      } 

      break;     
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (disp);
}


static void
gst_round_robin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRoundRobin *disp = GST_ROUND_ROBIN (object);
      
  GST_OBJECT_LOCK (disp);

  switch (prop_id) {
    case PROP_NUM_SRC_PADS:
      g_value_set_int (value, disp->nsrcpads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (disp);
}

static void
gst_round_robin_init (GstRoundRobin * disp)
{
  GstPad *pad;

  gst_element_create_all_pads (GST_ELEMENT (disp));
  pad = GST_PAD (GST_ELEMENT (disp)->sinkpads->data);

  GST_PAD_SET_PROXY_CAPS (pad);
  GST_PAD_SET_PROXY_SCHEDULING (pad);
  /* do not proxy allocation, it requires special handling like tee does */

  gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_round_robin_chain));
}

static void
gst_round_robin_class_init (GstRoundRobinClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = (GstElementClass *) klass;

  gst_element_class_set_metadata (element_class,
      "Round Robin", "Source/Network",
      "A round robin dispatcher element.",
      "Nicolas Dufresne <nicolas.dufresne@collabora.com");

  gobject_class->set_property = gst_round_robin_set_property;
  gobject_class->get_property = gst_round_robin_get_property;

  //set properties

  g_object_class_install_property (gobject_class, PROP_NUM_SRC_PADS,
      g_param_spec_int ("srcpads", "Num Src Pads",
          "The number of source pads", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RATE,
      gst_param_spec_array ("rate", "Rate Vector",
          "Probability of channels' use",
          g_param_spec_float ("prob1", "Prob1", "Probability Rate",
                  0.0, 1.0, 0.0,
                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REPETITION,
      gst_param_spec_array ("repetition", "Repetition Vector",
          "Probability of packets repetition for each channel",
          g_param_spec_float ("prob2", "Prob2", "Probability Repetition",
                  0.0, 1.0, 0.0,
                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PYTHON_RATE,
      g_param_spec_float("python-rate", "Rate Value",
          "A dynamic value of rate vector", 0.0, 1.0, 0.0,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PYTHON_REPETITION,
      g_param_spec_float("python-repetition", "Repetition Value",
          "A dynamic value of repetition vector", 0.0, 1.0, 0.0,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_templ);
  gst_element_class_add_static_pad_template (element_class, &src_templ);

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_round_robin_request_pad);
}