#include "sim/sim_pit.h"
#include <string.h>
#include "sim/sim_pic.h"
#include "sim/sim_state.h"

#define PIT_BASE 0x40u
#define PIT_CTRL 0x43u
static uint32_t load_value(const SimPitChannel *c){ return c->reload==0u?(c->bcd?10000u:65536u):c->reload; }
static uint16_t bcd_pack(uint32_t n){uint16_t v=0;unsigned i;for(i=0;i<4;i++){v|=(uint16_t)((n%10u)<<(4u*i));n/=10u;}return v;}
static uint32_t bcd_unpack(uint16_t v){uint32_t n=0,m=1;unsigned i;for(i=0;i<4;i++){n+=((v>>(4u*i))&15u)*m;m*=10u;}return n;}
static uint16_t visible(const SimPitChannel*c,uint32_t n){return c->bcd?bcd_pack(n):(uint16_t)n;}
static uint8_t request_byte(const SimBusRequest *request){return request->byte_enable==SIM_BUS_BYTE_ENABLE_HIGH?(uint8_t)(request->data>>8u):(uint8_t)request->data;}
static uint16_t response_byte(const SimBusRequest *request,uint8_t value){return request->byte_enable==SIM_BUS_BYTE_ENABLE_HIGH?(uint16_t)((uint16_t)value<<8u):value;}
const SimPitState *sim_pit_current_state(const SimPit*p,const SimState*s){return p&&p->attached?(const SimPitState*)sim_state_region_current_const(s,p->state_region):NULL;}
bool sim_pit_output(const SimPit*p,const SimState*s,unsigned channel){const SimPitState*state=sim_pit_current_state(p,s);return state!=NULL&&channel<3u&&state->channel[channel].out;}
void sim_pit_init(SimPit*p,SimPic*pic){if(!p)return;memset(p,0,sizeof(*p));p->input_clock_hz=1193182u;p->simulation_clock_hz=4772728u;p->pic=pic;for(unsigned i=0;i<3;i++)p->driven_gate[i]=true;}
void sim_pit_reset(SimPit*p){if(p)for(unsigned i=0;i<3;i++)p->driven_gate[i]=true;}
void sim_pit_set_clock(SimPit*p,uint32_t in,uint32_t sim){if(p&&in&&sim){p->input_clock_hz=in;p->simulation_clock_hz=sim;}}
void sim_pit_set_gate(SimPit*p,unsigned c,bool v){if(p&&c<3)p->driven_gate[c]=v;}
static bool probe(void*i,const SimBusRequest*r){(void)i;return r&&r->valid&&r->kind==SIM_BUS_IO&&r->address>=PIT_BASE&&r->address<=PIT_CTRL;}
static void evaluate(void*i,const SimState*c,const SimBusState*t,SimBusResponse*r){SimPit*p=i;const SimPitState*s=sim_pit_current_state(p,c);SimPitChannel*x;uint32_t v;unsigned n;if(!s||!t||!r)return;r->ready=true;if(t->request.direction==SIM_BUS_WRITE||t->request.address==PIT_CTRL)return;n=t->request.address-PIT_BASE;x=(SimPitChannel*)&s->channel[n];if(x->status_latch_valid){r->data=response_byte(&t->request,x->status_latch);return;}v=x->latch_valid?x->latch:x->count;v=visible(x,v);r->data=response_byte(&t->request,(uint8_t)(x->read_write==2u||((x->read_write==3u)&&x->read_high_next)?(v>>8u):v));}
static void reload(SimPitChannel*c){c->count=load_value(c);c->running=true;c->null_count=false;c->out=(c->mode==0u||c->mode==1u)?false:true;}
static void control(SimPitState*s,uint8_t v){unsigned n=(v>>6u)&3u,i;SimPitChannel*c;if(n==3u){for(i=0;i<3;i++)if(!(v&(1u<<(i+1u)))){c=&s->channel[i];if(!(v&0x20u)){c->latch=c->count;c->latch_valid=true;}if(!(v&0x10u)){c->status_latch=(uint8_t)((c->out?0x80u:0u)|(c->null_count?0x40u:0u)|(c->read_write<<4u)|(c->mode<<1u)|(c->bcd?1u:0u));c->status_latch_valid=true;}}return;}c=&s->channel[n];c->read_write=(v>>4u)&3u;c->mode=(v>>1u)&7u;if(c->mode>5u)c->mode&=3u;c->bcd=(v&1u)!=0;c->read_high_next=c->write_high_next=false;c->latch_valid=c->status_latch_valid=false;if(c->read_write==0u){c->latch=c->count;c->latch_valid=true;}else c->null_count=true;}
static void commit(void*i,const SimState*c,SimState*n,const SimBusRequest*r,const SimBusResponse*res){SimPit*p=i;SimPitState*s=p?(SimPitState*)sim_state_region_next(n,p->state_region):NULL;SimPitChannel*x;unsigned ch;uint8_t v;(void)c;(void)res;if(!p||!s||!r)return;if(r->direction==SIM_BUS_READ){if(r->address<PIT_CTRL){x=&s->channel[r->address-PIT_BASE];if(x->status_latch_valid)x->status_latch_valid=false;else if(x->read_write==3u){x->read_high_next=!x->read_high_next;if(!x->read_high_next)x->latch_valid=false;}else x->latch_valid=false;}return;}if(r->address==PIT_CTRL){control(s,request_byte(r));return;}ch=r->address-PIT_BASE;x=&s->channel[ch];v=request_byte(r);if(x->read_write==1u){x->reload=x->bcd?bcd_unpack(v):v;reload(x);}else if(x->read_write==2u){x->reload=x->bcd?bcd_unpack((uint16_t)v<<8u):(uint32_t)v<<8u;reload(x);}else if(!x->write_high_next){x->reload=(x->reload&0xFF00u)|(x->bcd?bcd_unpack(v):v);x->write_high_next=true;x->null_count=true;}else{x->reload=x->bcd?bcd_unpack((uint16_t)((visible(x,x->reload)&255u)|((uint16_t)v<<8u))):(x->reload&255u)|((uint32_t)v<<8u);x->write_high_next=false;reload(x);}}
static void tick(SimPitState*s,unsigned n,SimPic*pic){SimPitChannel*c=&s->channel[n];uint32_t reload_count=load_value(c);bool rising=c->gate&&!c->previous_gate;
    if((c->mode==1u||c->mode==5u)&&rising){reload(c);c->out=c->mode==5u;}
    if((c->mode==2u||c->mode==3u)&&!c->gate){c->out=true;return;}
    if((c->mode==2u||c->mode==3u)&&rising)reload(c);
    if(!c->running||!c->gate||!c->count)return;
    if(c->mode==2u){if(!c->out){c->out=true;c->count=reload_count>1u?reload_count-1u:1u;return;}if(c->count>1u){--c->count;return;}c->out=false;c->count=reload_count;if(n==0){s->irq0_pulse=true;sim_pic_pulse_irq(pic,0u);}return;}
    if(c->mode==3u){if(c->count>1u){--c->count;return;}if(c->out){c->out=false;c->count=reload_count/2u;}else{c->out=true;c->count=(reload_count+1u)/2u;}return;}
    if(c->count>1u){--c->count;return;}
    if(c->mode==4u||c->mode==5u){c->out=!c->out;if(c->out)c->running=false;else c->count=1u;}
    else{c->count=0u;c->out=true;c->running=false;}
}
static void sample(void*i,const SimState*c,SimState*n,const SimCycleResolution*r){SimPit*p=i;const SimPitState*o=sim_pit_current_state(p,c);SimPitState*s;unsigned j;(void)r;if(!p||!o||!p->simulation_clock_hz)return;s=(SimPitState*)sim_state_region_next(n,p->state_region);if(o->irq0_pulse&&p->pic!=NULL){sim_pic_set_irq_line(p->pic,0u,false);s->irq0_pulse=false;}for(j=0;j<3;j++){s->channel[j].previous_gate=o->channel[j].gate;s->channel[j].gate=p->driven_gate[j];}s->phase+=p->input_clock_hz;while(s->phase>=p->simulation_clock_hz){s->phase-=p->simulation_clock_hz;for(j=0;j<3;j++)tick(s,j,p->pic);}}
static void reset(void*i,SimState*s){SimPit*p=i;SimPitState z={0};unsigned n;if(!p)return;for(n=0;n<3;n++)z.channel[n].gate=true;*(SimPitState*)sim_state_region_current(s,p->state_region)=z;*(SimPitState*)sim_state_region_next(s,p->state_region)=z;}
SimBusTarget sim_pit_bus_target(SimPit*p,const char*n,uint32_t id){SimBusTarget t={0};t.name=n;t.target_id=id;t.instance=p;t.probe=probe;t.evaluate=evaluate;t.commit=commit;return t;}
bool sim_pit_attach(SimPit*p,SimKernel*k){SimModule m={0};SimPitState z={0};if(!p||!k||p->attached)return false;for(unsigned i=0;i<3;i++)z.channel[i].gate=true;if(!sim_state_add_region(k->state,"pit8254",sizeof(z),&z,&p->state_region))return false;p->attached=true;m.name="pit8254";m.instance=p;m.reset=reset;m.sample=sample;return sim_kernel_attach_module(k,&m);}
