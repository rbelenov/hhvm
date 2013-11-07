#include <vector>
#include <algorithm>

#include "vtune-jit.h"
#include "jitapi/jitprofiling.h"

namespace HPHP {
namespace Transl {

void reportTraceletToVtune(const Unit* unit, const Func* func, const TransRec& transRec)
{
    iJIT_Method_Load methodInfo;
    memset(&methodInfo, 0, sizeof(methodInfo));

    if (!unit) return;

    methodInfo.method_id = transRec.src.getFuncId() + 1000;

    if (func && func->fullName())
    {
        methodInfo.method_name = const_cast<char *>(func->fullName()->data());
    }
    else
    {
        methodInfo.method_name = const_cast<char *>("unknown");
    }

    methodInfo.source_file_name = const_cast<char *>(unit->filepath()->data());

    // aStart field of transRec.bcmapping may point to stubs range, so we need to explicitly
    // form mappings for main code and stubs

    size_t bcSize = transRec.bcMapping.size();
    std::vector<LineNumberInfo> mainLineMap, stubsLineMap;

    for(size_t i = 0; i < bcSize; i++)
    {
        LineNumberInfo info;

        info.LineNumber = unit->getLineNumber(transRec.bcMapping[i].bcStart);

        if (transRec.bcMapping[i].aStart >= transRec.aStart &&
            transRec.bcMapping[i].aStart < transRec.aStart + transRec.aLen)
        {
            info.Offset = transRec.bcMapping[i].aStart - transRec.aStart;
            mainLineMap.push_back(info);
        }
        else if (transRec.bcMapping[i].aStart >= transRec.astubsStart &&
            transRec.bcMapping[i].aStart < transRec.astubsStart + transRec.astubsLen)
        {
            info.Offset = transRec.bcMapping[i].aStart - transRec.astubsStart;
            stubsLineMap.push_back(info);
        }

        info.Offset = transRec.bcMapping[i].astubsStart - transRec.astubsStart;
        stubsLineMap.push_back(info);
    }

    auto infoComp = [&](const LineNumberInfo& a, const LineNumberInfo& b) -> bool {
        return a.Offset < b.Offset;
    };

    std::sort(mainLineMap.begin(), mainLineMap.end(), infoComp);
    std::sort(stubsLineMap.begin(), stubsLineMap.end(), infoComp);

    // Note that at this moment LineNumberInfo structures contain pairs of lines and code offset
    // for the start of the corresponding code, while JIT API treats the offset as the end of
    // this code (and the start offset is taken from the previous element or is 0); need to
    // shift the elements. Also, attribute the prologue (code before the first byte in the
    // mapping) to the first line.

    auto shiftLineMap = [&](std::vector<LineNumberInfo>& lineMap, unsigned regionSize) {
        if (lineMap.size() > 0)
        {
            LineNumberInfo tmpInfo;
            tmpInfo.Offset = regionSize;
            tmpInfo.LineNumber = lineMap.back().LineNumber;
            lineMap.push_back(tmpInfo);
            for (size_t i = lineMap.size() - 2; i > 0; i--)
            {
                lineMap[i].LineNumber = lineMap[i - 1].LineNumber;
            }
        }
    };

    shiftLineMap(mainLineMap, transRec.aLen);
    shiftLineMap(stubsLineMap, transRec.astubsLen);

    // Report main body

    methodInfo.method_load_address = transRec.aStart;
    methodInfo.method_size = transRec.aLen;
    methodInfo.line_number_size = mainLineMap.size();
    methodInfo.line_number_table = &mainLineMap[0];

    iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, (void *)&methodInfo);

    // Report stubs

    methodInfo.method_load_address = transRec.astubsStart;
    methodInfo.method_size = transRec.astubsLen;
    methodInfo.line_number_size = stubsLineMap.size();
    methodInfo.line_number_table = &stubsLineMap[0];

    iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, (void *)&methodInfo);
}

void reportTrampolineToVtune(void* begin, size_t size)
{
    iJIT_Method_Load methodInfo;
    memset(&methodInfo, 0, sizeof(methodInfo));

    methodInfo.method_id = 1000;

    methodInfo.method_name = const_cast<char *>("Trampoline");

    methodInfo.source_file_name = const_cast<char *>("Undefined");

    // Report main body

    methodInfo.method_load_address = begin;
    methodInfo.method_size = size;

    methodInfo.line_number_size = 0;
    methodInfo.line_number_table = 0;

    iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, (void *)&methodInfo);
}

}
}
