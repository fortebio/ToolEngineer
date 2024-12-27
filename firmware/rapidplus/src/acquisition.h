#ifndef _ACQUISITION_H
#define _ACQUISITION_H
#include <vector>
#include "./VEML6035/typedefinition.h"


class AcquisitionControl
{
    private:
        std::vector<Word> values;
        uint8_t repeats = 8;
        uint8_t maxErrors = 10;
        uint8_t numErrors = 0;
        double threshold = 3;
        double calculateMedian(std::vector<Word>& _values);
        void filterOdds(std::vector<Word>& _values);

    public:
        ~AcquisitionControl();
        AcquisitionControl();
        
        
        void store(Word value);
        bool isFinished();
        bool isMaxErrorReached();
        void setRepeats(uint8_t repeat);
        uint8_t getRepeats();
        void clear();
        double getAverage();
        Word getSum();
        void addErrorCount();
        void setNumMaxErrors(uint8_t maxErrors);
        uint8_t getNumMaxErrors();
        bool isClear();

};






extern AcquisitionControl _acquisition;
#endif