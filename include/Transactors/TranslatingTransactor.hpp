#pragma once

#include <queue>

#include <Transactor.hpp>
#include <Translator.hpp>


template <typename XLEN_t, bool strideAcrossPages>
class TranslatingTransactor final : public Transactor<XLEN_t> {

private:

    Translator<XLEN_t>* translator;
    Transactor<XLEN_t>* transactor;

public:

    TranslatingTransactor(Translator<XLEN_t>* translator, Transactor<XLEN_t>* transactor) :
        translator(translator), transactor(transactor) {
    }

    virtual inline Transaction<XLEN_t> Read(XLEN_t startAddress, XLEN_t size, char* buf) override {
        return TransactInternal<IOVerb::Read>(startAddress, size, buf);
    }

    virtual inline Transaction<XLEN_t> Write(XLEN_t startAddress, XLEN_t size, char* buf) override {
        return TransactInternal<IOVerb::Write>(startAddress, size, buf);
    }

    virtual inline Transaction<XLEN_t> Fetch(XLEN_t startAddress, XLEN_t size, char* buf) override {
        return TransactInternal<IOVerb::Fetch>(startAddress, size, buf);
    }

private:

    template <IOVerb verb>
    inline Transaction<XLEN_t> TransactInternal(XLEN_t startAddress, XLEN_t size, char* buf) {
        if constexpr (strideAcrossPages) {
            return TransactStriding<verb>(startAddress, size, buf);
        } else {
            return TransactSimple<verb>(startAddress, size, buf);
        }
    }

    template <IOVerb verb>
    inline Transaction<XLEN_t> TransactSimple(XLEN_t startAddress, XLEN_t size, char* buf) {

        Translation<XLEN_t> translation = translator->template Translate<verb>(startAddress);
        if (translation.generatedTrap != RISCV::TrapCause::NONE) {
            return { translation.generatedTrap, 0 };
        }

        // TODO is this the spec thing to do?
        XLEN_t maxSize = translation.validThrough - translation.untranslated + 1;
        if (maxSize < size) {
            size = maxSize;
        }

        return transactor->template Transact<verb>(translation.translated, size, buf);
    }

    template <IOVerb verb>
    inline Transaction<XLEN_t> TransactStriding(XLEN_t startAddress, XLEN_t size, char* buf) {

        Transaction<XLEN_t> result;
        result.trapCause = RISCV::TrapCause::NONE;
        result.transferredSize = 0;

        XLEN_t endAddress = startAddress + size - 1;

        if (endAddress < startAddress) {
            return result;
        }

        struct BufferedTransaction {
            XLEN_t startAddress;
            XLEN_t size;
            char* buf;
        };
        static std::queue<BufferedTransaction> transactionQueue;

        XLEN_t chunkStartAddress = startAddress;
        while (chunkStartAddress <= endAddress) {

            Translation<XLEN_t> translation = translator->template Translate<verb>(chunkStartAddress);

            result.trapCause = translation.generatedTrap;
            if (result.trapCause != RISCV::TrapCause::NONE) {
                while (!transactionQueue.empty()) {
                    transactionQueue.pop();
                }
                return result;
            }

            XLEN_t chunkEndAddress = translation.validThrough;
            if (chunkEndAddress > endAddress) {
                chunkEndAddress = endAddress;
            }
            XLEN_t chunkSize = chunkEndAddress - chunkStartAddress + 1;

            char* chunkBuf = buf + (chunkStartAddress - startAddress);
            XLEN_t translatedChunkStart = translation.translated + chunkStartAddress - translation.untranslated;
            transactionQueue.push({translatedChunkStart, chunkSize, chunkBuf});
            chunkStartAddress += chunkSize;

        }

        result.transferredSize = 0;
        while (!transactionQueue.empty()) {
            BufferedTransaction transaction = transactionQueue.front();
            transactionQueue.pop();
            Transaction<XLEN_t> chunkResult =
                transactor->template Transact<verb>(transaction.startAddress, transaction.size, transaction.buf);
            result.transferredSize += chunkResult.transferredSize;
            if (chunkResult.transferredSize != transaction.size) {
                break;
            }
        }

        return result;
    }

};
