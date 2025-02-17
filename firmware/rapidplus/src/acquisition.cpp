#include "acquisition.h"
#include <algorithm>


AcquisitionControl::AcquisitionControl()
{
}

AcquisitionControl::~AcquisitionControl()
{
}

void AcquisitionControl::store(Word value)
{
    if (value != 0 || value < 1000)
    {
        values.push_back(value);
    }
    else 
    {
        numErrors +=1;
    }
    if (values.size() == repeats)
    {
        filterOdds(values);   
    }
    
}

double AcquisitionControl::calculateMedian(std::vector<Word>& _values)
{
    // make a hard copy of vector to prevent sorting previous data
    std::vector<double> values_copy;
    for (double j : _values) {values_copy.push_back(j);}
    
    std::sort(values_copy.begin(), values_copy.end());
    // Find the median
    size_t size = values_copy.size();
    if (size % 2 == 0) {
        // If even, average the two middle elements
        return (values_copy[size / 2 - 1] + values_copy[size / 2]) / 2.0;
    } else {
        // If odd, return the middle element
        return values_copy[size / 2];
    }
}

void AcquisitionControl::filterOdds(std::vector<Word>& _values)
{
    std::vector<Word>::iterator it = _values.begin();
    double median = calculateMedian(_values);

    while(it != _values.end()) {

        if(abs(*it - median) > threshold) 
        {
            it = _values.erase(it);
            numErrors +=1;
        }
        else ++it;
    }
}

bool AcquisitionControl::isFinished()
{
    if (values.size() == repeats) { return true;} else {return false;}
}

bool AcquisitionControl::isMaxErrorReached()
{
    if (numErrors >= maxErrors) {return true;} else {return false;}
}

void AcquisitionControl::setRepeats(uint8_t repeat)
{
    repeats = repeat;
}

uint8_t AcquisitionControl::getRepeats()
{
    return repeats;
}

void AcquisitionControl::clear()
{
    values.clear();
    numErrors = 0;
}

double AcquisitionControl::getAverage()
{
    if (values.size() == 0) {return 0.0;}
    else
    {
        double mean = 0x0;
        for (double j : values) { mean += j;}
        mean /= values.size();
        return mean;
    }
}

Word AcquisitionControl::getSum()
{
    if (values.size() == 0) {return (Word)0;}
    else
    {
        Word _sum = (Word)0;
        for (Word j : values) { _sum += j;}
        return _sum;
    }
}

void AcquisitionControl::addErrorCount()
{
    numErrors +=1;
}

void AcquisitionControl::setNumMaxErrors(uint8_t _maxErrors)
{
    maxErrors = _maxErrors;
}

uint8_t AcquisitionControl::getNumMaxErrors()
{
    return maxErrors;
}

bool AcquisitionControl::isClear()
{
    if(values.size() == 0) {return true;} else {return false;};
}
