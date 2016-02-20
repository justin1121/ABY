/**
 \file 		arithmtmasking.h
 \author 	michael.zohner@ec-spride.de
 \copyright	ABY - A Framework for Efficient Mixed-protocol Secure Two-party Computation
			Copyright (C) 2015 Engineering Cryptographic Protocols Group, TU Darmstadt
			This program is free software: you can redistribute it and/or modify
			it under the terms of the GNU Affero General Public License as published
			by the Free Software Foundation, either version 3 of the License, or
			(at your option) any later version.
			This program is distributed in the hope that it will be useful,
			but WITHOUT ANY WARRANTY; without even the implied warranty of
			MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
			GNU Affero General Public License for more details.
			You should have received a copy of the GNU Affero General Public License
			along with this program. If not, see <http://www.gnu.org/licenses/>.
 \brief		arithmtmasking implementation.
 */
#ifndef __ARITHMTMASKING_H_
#define __ARITHMTMASKING_H_

#include "maskingfunction.h"

//#define DEBUGARITHMTMASKING
//TODO optimize

template<typename T>
class ArithMTMasking: public MaskingFunction {
public:
	ArithMTMasking(uint32_t numelements, CBitVector* in) {
		m_nElements = numelements; //=K
		m_vInput = in; //contains x and u, is 2-dim in case of the server and 1-dim in case of the client
		m_nMTBitLen = sizeof(T) * 8;
		memset(&m_nBitMask, 0xFF, sizeof(T));
		m_nOTByteLen = sizeof(T) * m_nElements;
		aesexpand = m_nOTByteLen > AES_BYTES;

		if (aesexpand) {
			m_bBuf = (BYTE*) malloc(sizeof(BYTE) * AES_BYTES);
			m_bCtrBuf = (BYTE*) malloc(sizeof(BYTE) * AES_BYTES);
			rndbuf.CreateBytes(PadToMultiple(m_nOTByteLen, AES_BYTES));
		}

	}
	;

	~ArithMTMasking() {
		free(m_bBuf);
		free(m_bCtrBuf);
		rndbuf.delCBitVector();
	}
	;

	//In total K' OTs will be performed
	void Mask(uint32_t progress, uint32_t len, CBitVector** values, CBitVector* snd_buf, snd_ot_flavor protocol) {

		//progress and processedOTs should always be divisible by MTBitLen
		if (progress % m_nMTBitLen != 0 || len % m_nMTBitLen != 0) {
			cerr << "progress or processed OTs not divisible by MTBitLen, cannot guarantee correct result. Progress = " << progress << ", processed OTs " << len
					<< ", MTBitLen = " << m_nMTBitLen << endl;
		}

		T tmpval, diff, gtmpval[m_nElements];

		for (uint32_t i = 0; i < m_nElements; i++)
			gtmpval[i] = 0;

#ifdef DEBUGARITHMTMASKING
		cout << "Starting" << endl;
		cout << "m_vInput.size= " << m_vInput->GetSize() << " progress = " << progress << ", mtbitlen = " << m_nMTBitLen << endl;
		m_vInput->PrintBinary();
#endif

		uint32_t startpos = (progress / (m_nMTBitLen * m_nElements));

		T* input = (T*) m_vInput->GetArr();
		T* rndval = (T*) snd_buf[0].GetArr();
		T* maskedval = (T*) snd_buf[1].GetArr();

		T* retvals = ((T*) values[0]->GetArr()) + startpos * m_nElements;

		for (uint32_t mtid = startpos, i = 0, mtbit, j, ctr = 0; i < len; mtid++) {
			diff = input[mtid]; //m_vInput->Get<T>(mtid * m_nMTBitLen, m_nMTBitLen);
#ifdef DEBUGARITHMTMASKING
					cout << "mtid = " << mtid << "; getting from " << mtbit * m_nMTBitLen << " to " << m_nMTBitLen << ", val = " << (UINT64_T) diff << endl;
#endif

			for (mtbit = 0; mtbit < m_nMTBitLen; mtbit++, i++) {
				for (j = 0; j < m_nElements; j++, ctr++) {
					//Get randomly generated mask from snd_buf[0]
					tmpval = rndval[ctr];
#ifdef DEBUGARITHMTMASKING
							cout << "S: i = " << i << ", diff " << (UINT64_T) diff << " tmpval = " << (UINT64_T)tmpval;
#endif
					//Add random mask to the already generated masks for this MT
					gtmpval[j] = gtmpval[j] + tmpval;
					tmpval = diff - tmpval;
#ifdef DEBUGARITHMTMASKING
					cout << ", added = " << (UINT64_T) tmpval << ", masked = " << (UINT64_T) snd_buf[1].Get<T>(i * m_nMTBitLen, m_nMTBitLen) << ", tmpsum mask = " << (UINT64_T) gtmpval[j] << endl;
#endif
					//Mask the resulting correlation with the second OT result
					maskedval[ctr] ^= tmpval;
				}
				diff = diff << 1;
			}

			//Write out the result into values[0]
			for (j = 0; j < m_nElements; j++, retvals++) {
#ifdef DEBUGARITHMTMASKING
				cout << "Computed Mask = " << (UINT64_T) gtmpval[j] << endl;
#endif
				retvals[0] = gtmpval[j];
				gtmpval[j] = 0;
			}
		}
	}
	;

	//rcv_buf holds the masked values that were sent by the sender, output holds the masks that were generated by the receiver

	//void UnMask(uint32_t progress, uint32_t processedOTs, CBitVector& choices, CBitVector& output, CBitVector& rcv_buf, CBitVector& tmpmasks, BYTE version) {
	void UnMask(uint32_t progress, uint32_t len, CBitVector* choices, CBitVector* output, CBitVector* rcv_buf, CBitVector* tmpmask, snd_ot_flavor version) {
		//progress and len should always be divisible by MTBitLen
		if (progress % m_nMTBitLen != 0 || len % m_nMTBitLen != 0) {
			cerr << "progress or processed OTs not divisible by MTBitLen, cannot guarantee correct result. Progress = " << progress << ", processed OTs " << len
					<< ", MTBitLen = " << m_nMTBitLen << endl;
		}
		//cout << "Unmasking " << len << " Elements " << endl;
		T tmpval, gtmpval[m_nElements];
		uint32_t lim = progress + len;
		BYTE* rcvbufptr = rcv_buf->GetArr();

		for (uint32_t i = 0; i < m_nElements; i++)
			gtmpval[i] = 0;

		uint32_t startpos = progress / (m_nMTBitLen * m_nElements);

		T* masks = (T*) tmpmask->GetArr();
		T* rcvedvals = (T*) rcv_buf->GetArr();
		T* outvals = ((T*) output->GetArr()) + startpos * m_nElements;

		for (uint32_t mtid = startpos, i = progress, mtbit, j; i < lim; mtid++) {
#ifdef DEBUGARITHMTMASKING
			cout << "Receiver val = " << (UINT64_T) tmpmasks.Get<T>(mtid * m_nMTBitLen, m_nMTBitLen) << ", bits = ";
			tmpmasks.Print(mtid * m_nMTBitLen, (mtid + 1) * m_nMTBitLen);
#endif
			for (mtbit = 0; mtbit < m_nMTBitLen; mtbit++, i++, rcvbufptr += m_nOTByteLen) {
				if (choices->GetBitNoMask(i)) {
					tmpmask->XORBytes(rcvbufptr, i * m_nOTByteLen, m_nOTByteLen);
					for (j = 0; j < m_nElements; j++) {
						tmpval = masks[i * m_nElements + j];
						gtmpval[j] = gtmpval[j] + tmpval;
#ifdef DEBUGARITHMTMASKING
						cout << "R: i = " << i << ", tmpval " << (UINT64_T) tmpval << ", tmpsum = " << (UINT64_T) gtmpval[j] << ", choice = " << (UINT64_T) choices.GetBitNoMask(i) << endl;
#endif
					}
				} else {
					for (j = 0; j < m_nElements; j++) {
						tmpval = masks[i * m_nElements + j];
						gtmpval[j] =
								gtmpval[j] - tmpval;
#ifdef DEBUGARITHMTMASKING
										cout << "R: i = " << i << ", tmpval " << (UINT64_T) tmpval << ", tmpsum = " << (UINT64_T) gtmpval[j] << ", choice = " << (UINT64_T) choices.GetBitNoMask(i) << endl;
#endif
					}
				}
			}

			//Write out the result into values[0]
			for (j = 0; j < m_nElements; j++, outvals++) {
#ifdef DEBUGARITHMTMASKING
				cout << "Computed = " << (UINT64_T) gtmpval[j] << endl;
#endif
				outvals[0] = gtmpval[j];
				gtmpval[j] = 0;
			}
		}
	}
	;

	//void expandMask(CBitVector& out, BYTE* sbp, uint32_t offset, uint32_t processedOTs, uint32_t bitlength, crypto* crypt) {
	void expandMask(CBitVector* out, BYTE* sbp, uint32_t offset, uint32_t processedOTs, uint32_t bitlength, crypto* crypt) {
		//the CBitVector to store the random values in

		if (!aesexpand) {
			BYTE* outptr = out->GetArr() + offset * m_nOTByteLen;
			for (uint32_t i = 0; i < processedOTs; i++, sbp += AES_KEY_BYTES, outptr += m_nOTByteLen) {
				memcpy(outptr, sbp, m_nOTByteLen);
			}
		} else {
			memset(m_bCtrBuf, 0, AES_BYTES);
			uint32_t* counter = (uint32_t*) m_bCtrBuf;
			for (uint32_t i = 0, rem; i < processedOTs; i++, sbp += AES_KEY_BYTES) {
				//Generate sufficient random bits
				crypt->init_aes_key(&tkey, sbp);
				for (counter[0] = 0; counter[0] < ceil_divide(m_nOTByteLen, AES_BYTES); counter[0]++) {
					crypt->encrypt(&tkey, m_bBuf, m_bCtrBuf, AES_BYTES);
					rndbuf.SetBytes(m_bBuf, counter[0] * AES_BYTES, AES_BYTES);
				}
				//Copy random bits into output vector
				out->SetBytes(rndbuf.GetArr(), (offset + i) * m_nOTByteLen, m_nOTByteLen);
			}
		}
	}

private:
	CBitVector* m_vInput;
	uint32_t m_nElements;
	uint32_t m_nOTByteLen;
	uint32_t m_nMTBitLen;
	uint64_t m_nBitMask;
	BYTE* m_bBuf;
	BYTE* m_bCtrBuf;
	AES_KEY_CTX tkey;
	BOOL aesexpand;
	CBitVector rndbuf;
};

#endif /* __ARITHMTMASKING_H_ */
