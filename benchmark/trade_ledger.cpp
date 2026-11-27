#include <cstdio>
#include <cstdint>
#include <algorithm>

struct Position { int64_t qty=0; double avg=0, realized=0; };   // qty 正多负空
struct Account  { double init=0, commission=0, realized=0, unrealized=0;
                  double equity() const { return init + realized + unrealized - commission; } };

static void update_pos(Position& p, bool buy, double price, int64_t q){
    if(buy){
        if(p.qty>=0){ p.avg=(p.avg*p.qty+price*q)/(p.qty+q); p.qty+=q; }
        else{ int64_t c=std::min<int64_t>(q,-p.qty); p.realized+=(p.avg-price)*c; p.qty+=c; int64_t r=q-c; if(r>0){p.avg=price;p.qty+=r;} if(p.qty==0)p.avg=0; }
    } else {
        if(p.qty<=0){ int64_t aq=-p.qty; p.avg=(p.avg*aq+price*q)/(aq+q); p.qty-=q; }
        else{ int64_t c=std::min<int64_t>(q,p.qty); p.realized+=(price-p.avg)*c; p.qty-=c; int64_t r=q-c; if(r>0){p.avg=price;p.qty-=r;} if(p.qty==0)p.avg=0; }
    }
}
static void on_fill(Position& p, Account& a, bool buy, double price, int64_t q, double feePerLot){
    update_pos(p, buy, price, q);
    a.realized   = p.realized;
    a.commission += feePerLot * q;
}
static void mark(Account& a, const Position& p, double last){ a.unrealized = (last - p.avg) * p.qty; }

static int g_fail=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %d %s\n",__LINE__,#c); ++g_fail; } }while(0)
static bool feq(double x,double y){ return (x>y?x-y:y-x) < 1e-6; }

int main(){
    // 多单加仓 → 均价 105
    { Position p; Account a; on_fill(p,a,true,100,10,0); on_fill(p,a,true,110,10,0);
      CHECK(p.qty==20 && feq(p.avg,105)); }
    // 平多 → 已实现 (120-100)*20=400
    { Position p; Account a; on_fill(p,a,true,100,20,0); on_fill(p,a,false,120,20,0);
      CHECK(p.qty==0 && feq(p.realized,400)); }
    // 空单买平 → (200-180)*5=100
    { Position p; Account a; on_fill(p,a,false,200,10,0); on_fill(p,a,true,180,5,0);
      CHECK(p.qty==-5 && feq(p.realized,100)); }
    // 多空翻转：多10@100 → 卖15@110（平10得100 + 空5@110）
    { Position p; Account a; on_fill(p,a,true,100,10,0); on_fill(p,a,false,110,15,0);
      CHECK(p.qty==-5 && feq(p.avg,110) && feq(p.realized,100)); }
    // 手续费 + 未实现 + 权益
    { Position p; Account a; a.init=10000; on_fill(p,a,true,100,10,5.0); mark(a,p,108);
      CHECK(feq(a.commission,50) && feq(a.unrealized,80) && feq(a.equity(),10030)); }
    printf(g_fail?"LEDGER FAILED:%d\n":"LEDGER ALL PASS\n", g_fail);
    return g_fail?1:0;
}
