#include "acquisition.h"
#include <algorithm>

/***********************************************************************
 * Function: AcquisitionControl()
 * Description: Default constructor for the AcquisitionControl class. Performs
 *  no explicit initialization; relies on in-class member defaults.
 * pramameter: none
 *  return: none
 */
AcquisitionControl::AcquisitionControl()
{
}

/***********************************************************************
 * Function: ~AcquisitionControl()
 * Description: Destructor for the AcquisitionControl class. Performs no
 *  cleanup.
 * pramameter: none
 *  return: none
 */
AcquisitionControl::~AcquisitionControl()
{
}

/***********************************************************************
 * Function: store()
 * Description: Stores one acquired sample. If the value passes the inline
 *  condition it is appended to the values vector, otherwise the error count
 *  is incremented. Once the number of stored values reaches repeats, the
 *  collected values are passed to filterOdds() for outlier removal.
 * pramameter: value - the acquired Word sample to store.
 *  return: none
 */
void AcquisitionControl::store(Word value)
{
    if (value != 0 || value < 1000)
    {
        values.push_back(value);
    }
    else
    {
        numErrors += 1;
    }
    if (values.size() == repeats)
    {
        filterOdds(values);
    }
}

/***********************************************************************
 * Function: calculateMedian()
 * Description: Computes the median of the given samples. Makes a sorted copy
 *  of the input (so the original order is preserved), then returns the
 *  average of the two middle elements when the count is even or the single
 *  middle element when odd.
 * pramameter: _values - reference to the vector of Word samples to evaluate.
 *  return: double - the median value of the samples.
 */
double AcquisitionControl::calculateMedian(std::vector<Word> &_values)
{
    // make a hard copy of vector to prevent sorting previous data
    std::vector<double> values_copy;
    for (double j : _values)
    {
        values_copy.push_back(j);
    }

    std::sort(values_copy.begin(), values_copy.end());
    // Find the median
    size_t size = values_copy.size();
    if (size % 2 == 0)
    {
        // If even, average the two middle elements
        return (values_copy[size / 2 - 1] + values_copy[size / 2]) / 2.0;
    }
    else
    {
        // If odd, return the middle element
        return values_copy[size / 2];
    }
}

/***********************************************************************
 * Function: filterOdds()
 * Description: Removes outlier samples from the vector. Computes the median
 *  via calculateMedian(), then erases every element whose absolute deviation
 *  from the median exceeds threshold, incrementing numErrors for each removed
 *  element.
 * pramameter: _values - reference to the vector of Word samples to filter
 *  in place.
 *  return: none
 */
void AcquisitionControl::filterOdds(std::vector<Word> &_values)
{
    std::vector<Word>::iterator it = _values.begin();
    double median = calculateMedian(_values);

    while (it != _values.end())
    {

        if (abs(*it - median) > threshold)
        {
            it = _values.erase(it);
            numErrors += 1;
        }
        else
            ++it;
    }
}

/***********************************************************************
 * Function: isFinished()
 * Description: Reports whether acquisition is complete by checking if the
 *  number of stored values equals the configured repeats count.
 * pramameter: none
 *  return: bool - true if values.size() equals repeats, otherwise false.
 */
bool AcquisitionControl::isFinished()
{
    if (values.size() == repeats)
    {
        return true;
    }
    else
    {
        return false;
    }
}

/***********************************************************************
 * Function: isMaxErrorReached()
 * Description: Reports whether the accumulated error count has reached or
 *  exceeded the configured maxErrors limit.
 * pramameter: none
 *  return: bool - true if numErrors >= maxErrors, otherwise false.
 */
bool AcquisitionControl::isMaxErrorReached()
{
    if (numErrors >= maxErrors)
    {
        return true;
    }
    else
    {
        return false;
    }
}

/***********************************************************************
 * Function: getRepeats()
 * Description: Returns the currently configured number of repeats.
 * pramameter: none
 *  return: uint8_t - the repeats member value.
 */
uint8_t AcquisitionControl::getRepeats()
{
    return repeats;
}

/***********************************************************************
 * Function: clear()
 * Description: Resets the acquisition state by clearing the values vector
 *  and setting numErrors back to 0.
 * pramameter: none
 *  return: none
 */
void AcquisitionControl::clear()
{
    values.clear();
    numErrors = 0;
}

/***********************************************************************
 * Function: getSum()
 * Description: Computes the sum of all stored values. Returns 0 when the
 *  values vector is empty; otherwise accumulates and returns the total.
 * pramameter: none
 *  return: Word - the sum of the stored values, or 0 if empty.
 */
Word AcquisitionControl::getSum()
{
    if (values.size() == 0)
    {
        return (Word)0;
    }
    else
    {
        Word _sum = (Word)0;
        for (Word j : values)
        {
            _sum += j;
        }
        return _sum;
    }
}

/***********************************************************************
 * Function: addErrorCount()
 * Description: Increments the acquisition error counter (numErrors) by one.
 * pramameter: none
 *  return: none
 */
void AcquisitionControl::addErrorCount()
{
    numErrors += 1;
}

/***********************************************************************
 * Function: getSizeValues()
 * Description: Returns the number of samples currently stored in the values
 *  vector, cast to uint8_t.
 * pramameter: none
 *  return: uint8_t - the current size of the values vector.
 */
uint8_t AcquisitionControl::getSizeValues()
{
    return uint8_t(values.size());
}

/***********************************************************************
 * Function: isClear()
 * Description: Reports whether the acquisition buffer is empty by checking
 *  if the values vector has size 0.
 * pramameter: none
 *  return: bool - true if values is empty, otherwise false.
 */
bool AcquisitionControl::isClear()
{
    if (values.size() == 0)
    {
        return true;
    }
    else
    {
        return false;
    };
}

/***********************************************************************
 * Function: fixValuesErrors()
 * Description: Pads the values vector up to the configured repeats count by
 *  repeatedly appending the middle element (index values.size()/2 captured
 *  before padding), so that downstream calculations have a full sample set.
 * pramameter: none
 *  return: none
 */
void AcquisitionControl::fixValuesErrors(void)
{
    uint8_t tmp = values.size() / 2;
    while (values.size() < repeats)
    {
        values.push_back(values[tmp]);
    }
}