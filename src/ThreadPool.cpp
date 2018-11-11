/*
 *
 * Author: Andreas Wüstenberg (andreas.wuestenberg@rwth-aachen.de)
 */

#include "rtps/ThreadPool.h"
#include "lwip/tcpip.h"

using rtps::ThreadPool;

bool ThreadPool::startThreads(){
    if(running){
        return true;
    }
    if(!inputQueue.init() || !outputQueue.init()){
        return false;
    }

    running = true;
    for(auto &thread : writers){
        // TODO ID, err check
        thread = sys_thread_new("WriterThread", writerFunction, this, Config::THREAD_POOL_WRITER_STACKSIZE, Config::THREAD_POOL_WRITER_PRIO);
    }
    return true;
}

void ThreadPool::stopThreads() {
    running = false;
}

void ThreadPool::clearQueues(){
    inputQueue.clear();
    outputQueue.clear();
}

bool ThreadPool::addConnection(const ip4_addr_t& addr, const ip4_port_t port) {
    return transport.createUdpConnection(addr, port, readCallback);
}

void ThreadPool::addWorkload(Writer& writer){
    inputQueue.moveElementIntoBuffer(&writer);
}

void ThreadPool::writerFunction(void* arg){
    auto pool = static_cast<ThreadPool*>(arg);
    if(pool == nullptr){
        printf("nullptr passed to writer function\n");
        return;
    }
    while(pool->running){
        {
            Writer* pWriter;
            auto isWorkToDo = pool->inputQueue.moveFirstInto(pWriter);
            if(!isWorkToDo){
                sys_msleep(1);
                continue;
            }
            PBufWrapper buffer;
            pWriter->createMessageCallback(buffer);

            /*

            PBufWrapper pbWrapper(current.size);
            if (!pbWrapper.isValid()) {
                printf("Error while allocating pbuf\n");
                continue;
            }


            const bool success = pbWrapper.append(current.data, current.size);
            if(!success){
                printf("Error while filling pbuf\n");
                continue;
            }

            pbWrapper.addr = current.addr;
            pbWrapper.port = current.port;
            pool->outputQueue.moveElementIntoBuffer(std::move(pbWrapper));

            // Execute with tcpip-thread
            tcpip_callback(sendFunction, pool); // Blocking i.e. thread safe call
             */
        }
    }
}

void ThreadPool::sendFunction(void* arg) {
    ThreadPool *pool = static_cast<ThreadPool*>(arg);
    if(pool == nullptr){
        printf("nullptr passed to send function\n");
        return;
    }
    PBufWrapper pBufWrapper;
    const bool isWorkToDo = pool->outputQueue.moveFirstInto(pBufWrapper);
    if(!isWorkToDo){
        printf("Who dares to wake me up if there is nothing to do?!");
        return;
    }
    pool->transport.sendPacket(pBufWrapper.addr, pBufWrapper.port, *pBufWrapper.firstElement);
}


void ThreadPool::readCallback(void*, udp_pcb*, pbuf* p, const ip_addr_t* addr, ip4_port_t port) {
    printf("Received something from %s:%u !!!!\n\r", ipaddr_ntoa(addr), port);
    for(int i=0; i < p->len; i++){
        printf("%c ", ((unsigned char*)p->payload)[i]);
    }
    printf("\n");
    pbuf_free(p);
}