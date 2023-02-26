# rucbase-lab实验报告

**2020200982 闫世杰**

## 实验内容:

### task1:实现并发b+树

根据实验指导实现简单的crabbing协议实现的悲观锁

- 对于读操作:先使用页级别的共享锁锁住根节点,搜索查找合适到的子节点后尝试获取子节点的共享锁,获得子节点共享锁后释放父节点上的共享锁，重复获子释父的操作逐渐向下搜索查找,直至到达叶子节点.
- 对于写操作:首先使用树级别的互斥锁root_latch锁住根节点,防止进程访问根节点变化的b+树.然后使用页级别的互斥锁锁住根结点,搜索查找合适到的子节点后尝试获取子节点的互斥锁，并检查节点是否安全,如果安全则说明其所有祖先节点均安全,则逐渐向上释放所有的祖先节点的互斥锁,同时根节点也安全,因此也可以释放树级别的互斥锁root_latch。重复操作逐渐向下搜索查找,直至到达叶子节点.当写操作完成后,需要释放所有仍锁住的页级互斥锁.

```c++
std::pair<IxNodeHandle*, bool> IxIndexHandle::FindLeafPage(const char *key, Operation operation, Transaction *transaction) {
    bool root_locked = operation==Operation::INSERT ||
                       operation==Operation::DELETE;
    IxNodeHandle *target = FetchNode( file_hdr_.root_page );
    if (operation == Operation::INSERT ||
        operation == Operation::DELETE )
    {
        target->page->WLatch();
        transaction->AddIntoPageSet(target->page);
    }
    else if( operation == Operation::FIND )
        target->page->RLatch();

    while( !target->page_hdr->is_leaf ) {
        IxNodeHandle *parent = target;
        target = FetchNode( target->InternalLookup(key) );
        if( operation == Operation::INSERT ||
            operation == Operation::DELETE )
        {
            target->page->WLatch();
            transaction->AddIntoPageSet(target->page); //自上往下锁写锁

            if( (operation==Operation::INSERT && 
                 target->GetSize()+1<target->GetMaxSize()) 
               &&
                (operation==Operation::DELETE && 
                 target->GetSize()-1>=(target->IsRootPage() ? 2 : target->GetMaxSize()/2))
            ) 
            /*------检查当前节点是否安全,安全则释放所有祖先点的写锁------*/
            {
                /*------向上检查并释放写锁------*/
                while( transaction->GetPageSet()->size() ) {
                    Page *page = transaction->GetPageSet()->front();
                    page->WUnlatch();
                    /*------根节点安全,解锁整棵树------*/
                    if(page->GetPageId().page_no == file_hdr_.root_page) { 
                        root_latch_.unlock();
                        root_locked = false;
                    }
                    transaction->GetPageSet()->pop_front();
                }
            }

        }
        else if( operation == Operation::FIND ) {
            /*------自上往下传递读锁------*/
            parent->page->RUnlatch();
            target->page->RLatch();
        }
        buffer_pool_manager_->UnpinPage(PAGEID(parent),false);
    }
    return {target,root_locked};
}
```

#### 优化操作

在设计乐观锁时,认为每次操作,无论读写都不会影响到b+树的结构,因此写操作在向下遍历搜索叶子的时候也同读操作一样传递共享锁,无需使用互斥锁.当到达叶子节点,发现写操作会影响树形结构时,则需要自根开始执行一次悲观锁的操作.

相比于悲观锁,乐观锁无需每次写操作都要对大量的互斥锁进行操作,而只有当子节点个数达到阈值时才会影响到树形结构,因此如果连续多次的插入/删除时,多次操作才会影响一次树形结构,此时乐观锁的效果确实会更好一点,但是如果插入/删除分配均衡时,乐观锁没有任何优势,甚至会表现的更差.

下面为乐观锁代码,测试发现并没有什么优势

```c++
while( 1 ) {
    /*------自上往下传递读锁------*/
    IxNodeHandle *parent = target;
    target = FetchNode( target->InternalLookup(key) );
    target->page->RLatch();
    parent->page->RUnlatch();
    // std::cout << "still while" << std::endl;
    if(target->page_hdr->is_leaf) {
        // std::cout << "find page" << std::endl;
        if( (operation==Operation::INSERT &&
             parent->GetSize()+1>=parent->GetMaxSize())
           ||
           (operation==Operation::DELETE &&
            parent->GetSize()-1<(parent->IsRootPage() ? 2 : parent->GetMaxSize()/2))
          )
            /*------会影响到树形结构------*/
        {
            root_locked = true;
            root_latch_.lock();
            IxNodeHandle *cur = FetchNode( file_hdr_.root_page );
            cur->page->WLatch();
            transaction->AddIntoPageSet(cur->page);
            while( !cur->page_hdr->is_leaf ) {
                cur = FetchNode( cur->InternalLookup(key) );
                cur->page->WLatch();
                transaction->AddIntoPageSet(cur->page); //自上往下锁写锁

                if( (operation==Operation::INSERT &&
                     cur->GetSize()+1<cur->GetMaxSize())
                   &&
                   (operation==Operation::DELETE &&
                    cur->GetSize()-1>=(cur->IsRootPage() ? 2 : cur->GetMaxSize()/2))
                  )
                    /*------检查当前节点是否安全,安全则释放所有祖先点的写锁------*/
                {
                    /*------向上检查并释放写锁------*/
                    while( transaction->GetPageSet()->size() ) {
                        Page *page = transaction->GetPageSet()->front();
                        page->WUnlatch();
                        /*------根节点安全,解锁整棵树------*/
                        if(page->GetPageId().page_no == file_hdr_.root_page) {
                            root_latch_.unlock();
                            root_locked = false;
                        }
                        transaction->GetPageSet()->pop_front();
                    }
                }
            }
        }
        break;
    }
}
```

#### 总结与建议:

- 频繁的进行读写锁的加锁与释放会造成大量时间消耗,同时由于数据集并不是很大,实现蟹型协议的优势并不能体现出来,因此细粒度的加锁与释放表现甚至不如粗粒度.
-  由于unpin操作会进行写回磁盘操作,相较于加锁与释放锁的时间,该操作耗时巨长,而在写操作时又需要频繁的进行unpin操作,因此大量的时间都会用在unpin操作上,而非加锁与释放

综上,个人认为通过总的时间消耗来衡量整个性能并不是一个很好的选择,个人感觉有两种改进方案

- 通过统计加锁与释放锁的次数来进行衡量实验的性能,比如加锁/释放一次共享锁 代价为x,加锁/释放依次互斥锁 代价为y,最后统计总代价来衡量并行的性能表现,不过xy的衡量值难以确定
- 在总时间的基础上,去掉写回磁盘操作的时间,这样就只剩下加放锁的消耗时间,该操作较为简单,可以统计unpin的次数,乘以每次unpin的时间即可得出unpin所花费时间,或者进行时间测试时不进行unpin操作

### task2:实现order by asc/desc limit 功能

词法分析:直接完全匹配即可, 在lex.l中添加关键字 order/by/asc/desc/limit的完全匹配即可

语法分析:在yacc.y中添加功能语句的语法匹配

order by colname asc/desc: 

匹配colname asc/desc作为一个排序条件,多个排序条件之间用逗号隔开,因此整个orderby语句匹配为:

```
OrderOp:
  { $$ = "ASC"; }
  | DESC { $$ = "DESC"; }
  | ASC { $$ = "ASC"; }
  ;
Order:
  colName OrderOp { $$ = std::make_shared<OrderExpr>($1, $2); }
  ;
Orders:
  { }
  | ORDER BY Order { $$ = std::vector<std::shared_ptr<OrderExpr>>{$3}; }
  | Orders ',' Order { $$.push_back($3); }
  ;
```

limit VALUE_INT: 每个select语句只有一个limit限制条件,语法较为简单:

```
Limit:
  { $$ = -1; }
  | LIMIT VALUE_INT { $$ = $2; }
```

修改ast.h中的selectstmt,将语法分析得到的colname asc/desc和limitvalue信息保存下来以便向后传递.

修改interp.h接收ast.h中传递过来的orderconditions和limitvalue

最后修改execution_manager.cpp中的print_record部分,先将select得到的record保存下来

**最后根据orderconditions对record进行排序,最后根据limit的限制进行print_record**

- 由于越靠前的orderby条件越起着决定性的作用,因此倒序遍历orderbycondition进行排序
- 先根据orderbycondition->colname找出排序条件所在的列
- 在根据排序值的大小以及orderbycondition->orderbyop(即DESC/ASC)进行排序

排序代码如下:

```c++
// order the record by ordercondition
for(auto ordercondition = orderconditions.rbegin(); ordercondition != orderconditions.rend(); ordercondition++) {
  auto colname = ordercondition->colname;
  auto orderop = ordercondition->orderop;
  int colline = captions.size()-1;
  for(;colline>=0;colline--)
      if(!colname.compare(captions[colline]))
          break;
  //sort by the orderby
  int len = selectrecords.size();
  for(int i=0; i<len; i++)
      for(int j=i+1; j<len; j++) {
          if( ( (selectrecords[i][colline]<selectrecords[j][colline] &&
                 !orderop.compare("DESC")) ||
                (selectrecords[i][colline]>selectrecords[j][colline] &&
                 !orderop.compare("ASC"))
              )
              ^
              (selectrecords[i][colline][0]=='-' && selectrecords[i][colline][0]=='-')
            )
              swap(selectrecords[i],selectrecords[j]);
      }
}
// print the record by the selectlimitnum
int lens = limitvalue==-1 ? selectrecords.size() : limitvalue;
for( int i=0; i < lens; i++ )
  rec_printer.print_record(selectrecords[i], context);
```

**注意:**在执行query_plan时,为了方便输出,所有的数据值都转化成了string(详情见exection_manager.cpp中的select_from函数中),在添加orderby功能时又需要对select record进行排序,如果数据值存在负数,直接使用strcompare会出问题,因此需要添加对负号的判断

#### 优化操作

最开始的想法是采用快排的方式优化排序方式,但是快排无法解决select record非常多,而limitvalue非常小的情况,出现select record非常多,而limitvalue非常小的情况时,此时会浪费大量的时间做无用的排序,针对该种情形,可以使用堆排序做出优化,从而大大减少时间.具体操作是,可以使用基于对排序的优先队列来作为储存select record的容器,只需自定义大小关系即可.此时,时间代价为O(klogn),其中k=limitvalue,与冒泡的O(n^2)和快排的O(nlogn)相比有着较大的优势

### 课程体会及心得:

整个实验做下来之后还是很有收获的,确实也是理解和实现了数据库的底层工作原理,在lab3完成之后,测试自己写的sql语句时有一种丰收的喜悦,毕竟自己动手完成一个数据库是一个不小的工程,这么大的工程量在目前学过的所有的课程里面也算是比较少见的.同时对之前其他课程所学的内容也有了更深入的了解与体会比如数据结构里的b+树,还有调度算法里的替换策略.

不过感觉整体实验和课堂上所学到的理论之间还是存在着一些gap,感觉就lab4与课堂上学的理论相关性较大,前面的几个lab相关性都比较小,课堂上学的理论,如果没有其他事情影响的话还是应该通过考试的方式来进行考察.

对于这种补充性的实验,感觉可以参考一下ics2的fslab(实现一个文件管理系统),fslab的自主设计性比rucbaselab要高得多,fslab只提供了很少的底层接口,整个文件系统的管理设计都是由学生自主设计完成,不过由于是两个实验的复杂性不一样,rucbaselab很难做到像fslab一样,但感觉还是可以借鉴和参考一下fslab的设计思路,希望能对rucbaselab的改进提供一些思路.

### 课程反馈及意见:

看得出来老师和师兄师姐们在实验上付出了很多的心思,对于实验中的个个问题也给出了详细的回答和指导,努力把rucbaselab设计成为一个符合人大金课标准的实验.不过由于是第一年设计并采用rucbase-lab作为课程实验,该实验还存在一些问题

由于是采用填补代码的方法来完成实验,因此实验的很大一部分任务在于阅读各种头文件来了解和熟悉各种接口,实验的上手难度较大,尤其是lab1task2缓冲池替换策略和lab1task3缓冲池管理器两个实验,不过由于助教给出了较为详细的实验指导,因此在熟悉接口之后,填补代码时较为简单.但有时接口非常多,如lab3,如果实验指导不给出接口的使用,只靠自己阅读头文件中的各种类定义,需要花费大量的时间,实验指导又过于详细(比如lab3),导致就算什么都不会,照着实验指导填补代码就可以完成任务,这两者之间很难做一个平衡.

整个学期的实验做下来之后,感觉每个lab都是花费时间在接口阅读上,然后根据助教写的实验指导来完成任务,实验的意义在于了解和实现数据库的底层工作原理,而不是通过阅读接口来进行照猫画虎,有种本末倒置的感觉,

这很大一部分原因是代码填空这种方式带来的,没有什么较好的解决方式,或许可以减少实验指导的详细程度,让学生自行阅读来了解接口不过会一定程度上增大学生的任务量.或者可以给出详细的接口说明,但是针对每次实验设计一次答辩,确保学生对实验的内容有着一定的理解,而不是照猫画虎的完成任务.~~也可以重新设计一下实验,通过给出input接受 output的方式来进行实验(感觉不太现实)~~.或者可以改成一个像fslab一样的功能补充实验,代码填空实验的效果确实不是很好.因为0->1实验带来的成就感以及收获确实比代码填空实验更多一些.

总体来说,实验的目的还是达到了,通过实验确实是对数据库的底层实现有着更加深入的了解,考虑到是第一次设计和使用该实验,整体的设计还是很新颖的,相信经过不断地完善和改变,该实验会越来越好的.
