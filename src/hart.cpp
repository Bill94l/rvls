/*
 * hart.h
 *
 *  Created on: Aug 1, 2023
 *      Author: rawrr
 */

#include "hart.hpp"



SpikeIf::SpikeIf(CpuMemoryView *memory, const cfg_t *cfg)
    : memory(memory), cfg(cfg) {

}

Region* SpikeIf::getRegion(u64 address){
    for(auto &r : regions){
        if(address >= r.base && address < r.base + r.size) {
            return &r;
        }
    }
    return NULL;
}

bool SpikeIf::isMem(u64 address){
    auto r = getRegion(address);
    return r != NULL && r->type == RegionType::mem;
}

bool SpikeIf::isIo(u64 address){
    auto r = getRegion(address);
    return r != NULL && r->type == RegionType::io;
}

bool SpikeIf::isFetchable(u64 address){
    auto r = getRegion(address);
    return r != NULL;
}

// should return NULL for MMIO addresses
char* SpikeIf::addr_to_mem(reg_t addr)  {
//        if((addr & 0xE0000000) == 0x00000000) return NULL;
//        printf("addr_to_mem %lx ", addr);
//        return (char*) memory->get(addr);
    return NULL;
}

// used for MMIO addresses
bool SpikeIf::mmio_fetch(reg_t addr, size_t len, u8* bytes)  {
    if(isFetchable(addr)) {
        memory->fetch(addr, len, bytes);
        return true;
    }
    return false;
}


bool SpikeIf::mmio_mmu(reg_t addr, size_t len, u8* bytes)  {
    if(isMem(addr)) {
        memory->mmu(addr, len, bytes);
        return true;
    }
    return false;
}

bool SpikeIf::mmio_load(reg_t addr, size_t len, u8* bytes)  {
    if(isMem(addr)) {
        memory->load(addr, len, bytes);
        return true;
    }
    if(isIo(addr)){
    //        printf("mmio_load %lx %ld\n", addr, len);
        if(addr < 0x10000000 || addr > 0x20000000) return false;
        assertTrue("missing mmio\n", !ioQueue.empty());
        auto dut = ioQueue.front();
        assertEq("mmio write\n", dut.write, false);
        assertEq("mmio address\n", dut.address, addr);
        assertEq("mmio len\n", dut.size, len);
        memcpy(bytes, (u8*)&dut.data, len);
        ioQueue.pop();
        return !dut.error;
    }

    return false;
}
bool SpikeIf::mmio_store(reg_t addr, size_t len, const u8* bytes)  {
    if(isMem(addr)) {
        memory->store(addr, len, (u8*) bytes);
        return true;
    }

    if(isIo(addr)){
    //        printf("mmio_store %lx %ld\n", addr, len);
        if(addr < 0x10000000 || addr > 0x20000000) return false;
        assertTrue("missing mmio\n", !ioQueue.empty());
        auto dut = ioQueue.front();
        assertEq("mmio write\n", dut.write, true);
        assertEq("mmio address\n", dut.address, addr);
        assertEq("mmio len\n", dut.size, len);
        assertTrue("mmio data\n", !memcmp((u8*)&dut.data, bytes, len));
        ioQueue.pop();
        return !dut.error;
    }

    return false;
}
// Correct definition of get_cfg without virtual and override
const cfg_t& SpikeIf::get_cfg() const {
    return *cfg;
}
// Correct definition of get_harts without virtual and override
const std::map<size_t, processor_t*>& SpikeIf::get_harts() const {
    return harts;
}
// Callback for processors to let the simulation know they were reset.
void SpikeIf::proc_reset(unsigned id)  {
//        printf("proc_reset %d\n", id);
}

const char* SpikeIf::get_symbol(uint64_t addr)  {
//        printf("get_symbol %lx\n", addr);
    return NULL;
}



Hart::Hart(u32 hartId, string isa, string priv, u32 physWidth, CpuMemoryView *memory, FILE *logs)
: isa_hart(isa), priv_hart(priv), cfg() // Initialiser le membre cfg
{
    this->memory = memory;
    this->physWidth = physWidth;
    std::ofstream outfile ("/dev/null",std::ofstream::binary);
    this->cfg.isa = isa_hart.c_str();
    this->cfg.priv = priv_hart.c_str(); 
    this->cfg.misaligned = false;
    this->cfg.pmpregions = 0;
    this->cfg.hartids.push_back(hartId);
    sif = new SpikeIf(memory, &this->cfg);
    proc = new processor_t(isa_hart.c_str(), priv_hart.c_str(), &this->cfg, sif, hartId, false, logs, outfile);    auto xlen = proc->get_xlen();
    proc->set_impl(IMPL_MMU_SV32, xlen == 32);
    proc->set_impl(IMPL_MMU_SV39, xlen == 64);
    proc->set_impl(IMPL_MMU_SV48, false);
    proc->set_impl(IMPL_MMU, true);
    proc->set_pmp_num(0);
    state = proc->get_state();
    state->csrmap[CSR_MCYCLE] = std::make_shared<basic_csr_t>(proc, CSR_MCYCLE, 0);
    state->csrmap[CSR_MCYCLEH] = std::make_shared<basic_csr_t>(proc, CSR_MCYCLEH, 0);
}

void Hart::close() {
    auto f = proc->get_log_file();
    if(f) fclose(f);
}

void Hart::setPc(u64 pc){
    state->pc = pc;
}

void Hart::writeRf(u32 rfKind, u32 address, u64 data){
    switch(rfKind){
    case 0:
        integerWriteValid = true;
        integerWriteData = data;
        break;
    case 1:
        floatWriteValid = true;
        floatWriteData = data;
        break;
    case 4:
//        if((csrAddress == CSR_FCSR || csrAddress == CSR_FRM || csrAddress == CSR_FFLAGS) && address == CSR_MSTATUS){
//            fsDirty = true;
//            fsCsrAddress = address;
//            break;
//        }
        if((csrWrite || csrRead) && csrAddress != address){
        	failure("duplicated CSR access \n");
        }
        csrAddress = address;
        csrWrite = true;
        csrWriteData = data;
        break;
    default:
    	failure("??? unknown RF trace \n");
    	break;
    }

}


void Hart::readRf(u32 rfKind, u32 address, u64 data){
    switch(rfKind){
    case 4:
        if((csrWrite || csrRead) && csrAddress != address){
        	failure("duplicated CSR access \n");
        }
        csrAddress = address;
        csrRead = true;
        csrReadData = data;
        break;
    default:
    	failure("??? unknown RF trace \n");
        break;
    }

}

void Hart::physExtends(u64 &v){
    v = (u64)(((s64)v<<(64-physWidth)) >> (64-physWidth));
}

void Hart::trap(bool interrupt, u32 code, u64 address){
    int mask = 1 << code;
    auto fromPc = state->pc;
    bool pageFault = !interrupt && (code == 12 || code == 13 || code == 15);
    //printf("DUT did trap at tval: 0x%lx pc: %lx code %d\n", address, fromPc, code);
     if(pageFault){
        auto mmu = proc->get_mmu();
        mmu->flush_tlb();
        mmu->fault_fetch = code == 12;
        mmu->fault_load  = code == 13;
        mmu->fault_store = code == 15;
        mmu->fault_address = address;
    }

    if(interrupt) state->mip->write_with_mask(mask, mask);
    proc->step(1);
    if(interrupt) state->mip->write_with_mask(mask, 0);
    if(pageFault){
        auto mmu = proc->get_mmu();
        mmu->fault_fetch = false;
        mmu->fault_load  = false;
        mmu->fault_store = false;
    }
    if(!state->trap_happened){
//        printf("DUT did trap on %lx Code %d\n", fromPc, code);
        failure("DUT did trap on %lx\n", fromPc);
    }

    memory->step();
    assertEq("DUT interrupt missmatch", interrupt, state->trap_interrupt);
    assertEq("DUT code missmatch", code, state->trap_code);
    physExtends(state->pc);
}

void Hart::commit(u64 pc){
	//auto shift = 64-proc->get_xlen();
    //if(pc != (state->pc << shift >> shift)){
    if(pc != state->pc ){
    	failure("PC MISSMATCH dut=%lx ref=%lx\n", pc, state->pc);
    }

    //Sync CSR
    u64 csrBackup = 0;
    if(csrRead){
        switch(csrAddress){
        case CSR_MCYCLE:
        case CSR_MCYCLEH:
        case CSR_UCYCLE:
        case CSR_UCYCLEH:
            state->csrmap[csrAddress]->unlogged_write(csrReadData);
            break;
        case MIP:
        case SIP:
        case UIP:
            csrBackup = state->mie->read();
            state->mip->unlogged_write_with_mask(-1, csrReadData);
            state->mie->unlogged_write_with_mask(MIE_MTIE | MIE_MEIE |  MIE_MSIE | MIE_SEIE, 0);
//                                cout << main_time << " " << hex << robCtx.csrReadData << " " << state->mip->read()  << " " << state->csrmap[robCtx.csrAddress]->read() << dec << endl;
            break;
        }
        if(csrAddress >= CSR_MHPMCOUNTER3 && csrAddress <= CSR_MHPMCOUNTER31){
            state->csrmap[csrAddress]->unlogged_write(csrReadData);
        }
    }

    if(scValid){
        scValid = false;
        if(scFailure) {
            proc->get_mmu()->yield_load_reservation();
        } else {
            //Assume spike and dut match, but not necessarily true
        }
    }

    //Run the spike model
    proc->step(1);
    memory->step();

    //Sync back some CSR
    state->mip->unlogged_write_with_mask(-1, 0);
    if(csrRead){
        switch(csrAddress){
        case MIP:
        case SIP:
        case UIP:
            state->mie->unlogged_write_with_mask(MIE_MTIE | MIE_MEIE |  MIE_MSIE | MIE_SEIE, csrBackup);
            break;
        }
    }

    //Checks
//        printf("%016lx %08lx\n", pc, state->last_inst.bits());
    assertTrue("DUT missed a trap", !((u32)state->trap_happened));
    for (auto item : state->log_reg_write) {
        if (item.first == 0)
          continue;

        u32 rd = item.first >> 4;
        switch (item.first & 0xf) {
        case 0: { //integer
            assertTrue("INTEGER WRITE MISSING", integerWriteValid);
            assertEq("INTEGER WRITE MISSMATCH", integerWriteData, item.second.v[0]);
            integerWriteValid = false;
        } break;
        case 1: { //float
            assertTrue("FLOAT WRITE MISSING", floatWriteValid);
            assertEq("FLOAT WRITE MISSMATCH", floatWriteData, item.second.v[0]);
            floatWriteValid = false;
        } break;
        case 4:{ //CSR
            u64 inst = state->last_inst.bits();
            switch(inst){
            case 0x30200073: //MRET
            case 0x10200073: //SRET
            case 0x00200073: //URET
                physExtends(state->pc);
                break;
            default:{
                if((inst & 0x7F) == 0x73 && (inst & 0x3000) != 0){
                    if((inst >> 20)==CSR_FCSR || (inst >> 20)==CSR_FRM || (inst >> 20)==CSR_FFLAGS){
                        if(rd!=CSR_MSTATUS){
                            assertTrue("CSR WRITE MISSING", csrWrite);
                            assertEq("CSR WRITE ADDRESS", (u32)(csrAddress & 0xCFF), (u32)(rd & 0xCFF));
        //                                                assertEq("CSR WRITE DATA", whitebox->robCtx[robId].csrWriteData, item.second.v[0]);
                        }
                        break;
                    }
                    assertTrue("CSR WRITE MISSING", csrWrite);
                    assertEq("CSR WRITE ADDRESS", (u32)(csrAddress & 0xCFF), (u32)(rd & 0xCFF));
                    /*
                    assertTrue("CSR WRITE MISSING", csrWrite || fsDirty);
                    if(csrWrite){ assertEq("CSR WRITE ADDRESS", (u32)(csrAddress & 0xCFF), (u32)(rd & 0xCFF));
                    }else if(fsDirty){
                        assertEq("CSR WRITE ADDRESS", (u32)(fsCsrAddress & 0xCFF), (u32)(rd & 0xCFF));
                        fsDirty = false;
                    }
                    */
                }
            }

            }
            csrWrite = false;
        } break;
        default: {
            failure("??? unknown spike trace %lx\n", item.first & 0xf);
        } break;
        }
    }

    csrRead = false;
    assertTrue("CSR WRITE SPAWNED", !csrWrite);
    assertTrue("INTEGER WRITE SPAWNED", !integerWriteValid);
    assertTrue("FLOAT WRITE SPAWNED", !floatWriteValid);
}

void Hart::ioAccess(TraceIo io){
    sif->ioQueue.push(io);
}

void Hart::setInt(u32 id, bool value){

}

void Hart::scStatus(bool failure){
    scValid = true;
    scFailure = failure;
}

void Hart::addRegion(Region r){
//    printf("Type: %d Base %lx Size %lx\n", r.type, r.base, r.size);
    sif->regions.push_back(r);
}
