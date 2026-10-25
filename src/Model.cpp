/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Model.h>

namespace vsgOcean
{
    Model::Model(const std::string& in_name, const vsg::dmat4& in_matrix) :
        Inherit(in_name, in_matrix)
    {
    }

    void Model::addFrame(const vsg::ref_ptr<Frame>& frame, const std::string& parentFrameName)
    {
        if (!frame) return;

        addChild(frame);

        if (frame->name.empty() || frames_.count(frame->name))
        {
            vsg::warn("vsgOcean::Model::addFrame - ", frame->name.empty() ? "empty" : "duplicate", " frame name in model ", name);
            return;
        }

        frames_[frame->name] = frame;
        // an empty parent frame name means the model's own base frame
        frameParents_[frame->name] = parentFrameName.empty() ? name : parentFrameName;
    }

    void Model::addModel(const vsg::ref_ptr<Model>& submodel)
    {
        if (!submodel) return;

        submodel->setParentName(name);
        addChild(submodel);

        if (submodel->name.empty() || models_.count(submodel->name))
        {
            vsg::warn("vsgOcean::Model::addModel - ", submodel->name.empty() ? "empty" : "duplicate", " model name in model ", name);
            return;
        }

        models_[submodel->name] = submodel;
    }

    vsg::ref_ptr<Frame> Model::frame(const std::string& frameName) const
    {
        if (frameName == name) return vsg::ref_ptr<Frame>(const_cast<Model*>(this));

        auto itr = frames_.find(frameName);
        if (itr != frames_.end()) return itr->second;

        for (const auto& [modelName, submodel] : models_)
        {
            if (auto frame = submodel->frame(frameName)) return frame;
        }

        return {};
    }

    vsg::ref_ptr<Model> Model::model(const std::string& modelName) const
    {
        if (modelName == name) return vsg::ref_ptr<Model>(const_cast<Model*>(this));

        for (const auto& [subName, submodel] : models_)
        {
            if (auto model = submodel->model(modelName)) return model;
        }

        return {};
    }

    void Model::setPose(const std::string& frameName, const vsg::dmat4& pose, const std::string& parentFrameName)
    {
        if (frameName == name)
        {
            if (!parentFrameName.empty() && parentFrameName != parentName_)
            {
                vsg::warn("vsgOcean::Model::setPose - model ", name, " is attached to ", parentName_.empty() ? "the world" : parentName_, ", not ", parentFrameName);
            }
            matrix = pose;
            return;
        }

        auto itr = frames_.find(frameName);
        if (itr != frames_.end())
        {
            if (!parentFrameName.empty() && parentFrameName != frameParents_[frameName])
            {
                vsg::warn("vsgOcean::Model::setPose - frame ", frameName, " is attached to ", frameParents_[frameName], ", not ", parentFrameName);
            }
            itr->second->matrix = pose;
            return;
        }

        for (const auto& [subName, submodel] : models_)
        {
            if (submodel->frame(frameName))
            {
                submodel->setPose(frameName, pose, parentFrameName);
                return;
            }
        }

        vsg::warn("vsgOcean::Model::setPose - unknown frame ", frameName, " in model ", name);
    }
}
